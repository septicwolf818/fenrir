#pragma once
// Terminal — basic terminal emulator in a Fenrir window.
// Runs a real shell ($SHELL or /bin/sh) in a pseudoterminal (forkpty)
// and draws its output in a cell grid. Supported VT100/xterm subset:
// cursor positioning, line clearing/editing, scrolling regions,
// alternate screen (1049), application cursor mode (h/l), SGR 16/256 colors,
// reverse video, UTF-8. Key translation to xterm sequences.
// Alt+Tab switches windows; Esc is forwarded to the shell (an exception to
// the general Esc reservation rule, documented in this app).
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include "fenrir/app/App.hpp"
#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Types.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// --- Virtual grid + VT100/xterm subset --------------------------------------

struct VtCell {
    char32_t ch = U' ';
    Color fg = colors::White;
    Color bg = colors::Black;
    uint8_t style = StyleNone; // bit 7 (internal) = reverse video
};

class VtScreen {
public:
    // Reverse-video bit — stored in the cell style, read when drawing.
    static constexpr uint8_t kReverse = 0x80;

    void resize(int w, int h) {
        w_ = std::max(1, w);
        h_ = std::max(1, h);
        const std::size_t n = static_cast<std::size_t>(w_ * h_);
        grid_.assign(n, VtCell{});
        altGrid_.assign(n, VtCell{});
        cx_ = cy_ = 0;
        top_ = 0;
        bot_ = h_ - 1;
    }

    int width() const { return w_; }
    int height() const { return h_; }
    int cursorX() const { return cx_; }
    int cursorY() const { return cy_; }
    bool cursorVisible() const { return cursorVisible_; }
    bool appCursorKeys() const { return appCursor_; }

    const VtCell& at(int x, int y) const {
        static const VtCell blank;
        if (x < 0 || y < 0 || x >= w_ || y >= h_) return blank;
        return buf()[static_cast<std::size_t>(y * w_ + x)];
    }

    void feed(const char* data, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char b = static_cast<unsigned char>(data[i]);
            switch (state_) {
                case St::Ground: ground(b); break;
                case St::Utf8: utf8(b); break;
                case St::Esc: esc(b); break;
                case St::Csi: csi(b); break;
                case St::CsiIgnore:
                    if (b == 0x1b) state_ = St::Esc;
                    else if (b >= 0x40 && b <= 0x7e) state_ = St::Ground;
                    break;
                case St::Osc: // window title — skip until BEL or ST
                    if (b == 0x07) state_ = St::Ground;
                    else if (b == 0x1b) state_ = St::OscEsc;
                    break;
                case St::OscEsc: state_ = (b == '\\') ? St::Ground : St::Osc; break;
                case St::Charset: state_ = St::Ground; break; // character set selection — ignore
            }
        }
    }

private:
    enum class St { Ground, Utf8, Esc, Csi, CsiIgnore, Osc, OscEsc, Charset };

    std::vector<VtCell>& buf() { return altOn_ ? altGrid_ : grid_; }
    const std::vector<VtCell>& buf() const { return altOn_ ? altGrid_ : grid_; }

    void ground(unsigned char b) {
        switch (b) {
            case 0x1b: state_ = St::Esc; return;
            case '\r': cx_ = 0; return;
            case '\n':
            case 0x0b:
            case 0x0c: index_(); return;
            case '\b': if (cx_ > 0) --cx_; return;
            case '\t': cx_ = std::min(w_ - 1, ((cx_ / 8) + 1) * 8); return;
            default: break;
        }
        if (b < 0x20 || b == 0x7f) return; // C0 / DEL — ignore
        if (b < 0x80) {
            put(static_cast<char32_t>(b));
            return;
        }
        // lead UTF-8: 110xxxxx / 1110xxxx / 11110xxx
        if ((b & 0xe0) == 0xc0) { utf8Cp_ = b & 0x1f; utf8Left_ = 1; }
        else if ((b & 0xf0) == 0xe0) { utf8Cp_ = b & 0x0f; utf8Left_ = 2; }
        else if ((b & 0xf8) == 0xf0) { utf8Cp_ = b & 0x07; utf8Left_ = 3; }
        else return; // continuation byte alone — ignore
        state_ = St::Utf8;
    }

    void utf8(unsigned char b) {
        if ((b & 0xc0) != 0x80) { // interrupted sequence — return to Ground
            state_ = St::Ground;
            ground(b);
            return;
        }
        utf8Cp_ = (utf8Cp_ << 6) | (b & 0x3f);
        if (--utf8Left_ == 0) {
            put(static_cast<char32_t>(utf8Cp_));
            state_ = St::Ground;
        }
    }

    void esc(unsigned char b) {
        state_ = St::Ground;
        switch (b) {
            case '[': params_.clear(); param_ = 0; hasParam_ = false; priv_ = false; state_ = St::Csi; break;
            case ']': state_ = St::Osc; break;
            case '(': case ')': case '*': case '+': state_ = St::Charset; break;
            case '7': savedX_ = cx_; savedY_ = cy_; break; // DECSC
            case '8': cx_ = std::clamp(savedX_, 0, w_ - 1); cy_ = std::clamp(savedY_, 0, h_ - 1); break; // DECRC
            case 'M': reverseIndex_(); break; // RI
            case 'D': index_(); break;        // IND
            case 'E': cx_ = 0; index_(); break; // NEL
            case 'c': hardReset(); break;     // RIS
            default: break; // '=', '>', 'H', 'Z' — ignore
        }
    }

    void csi(unsigned char b) {
        if (b == '?') { priv_ = true; return; } // private prefix (DECSET/DECRST)
        if (b >= '0' && b <= '9') {
            param_ = std::min(param_ * 10 + (b - '0'), 9999);
            hasParam_ = true;
            return;
        }
        if (b == ';') {
            if (params_.size() >= kMaxCsiParams) { state_ = St::CsiIgnore; return; }
            params_.push_back(hasParam_ ? param_ : 0);
            param_ = 0;
            hasParam_ = false;
            return;
        }
        if (b >= 0x20 && b <= 0x2f) return; // intermediate bytes — ignore
        if (b < 0x40 || b > 0x7e) {
            state_ = (b == 0x1b) ? St::Esc : St::Ground;
            return;
        }
        if (hasParam_) {
            if (params_.size() >= kMaxCsiParams) { state_ = St::Ground; return; }
            params_.push_back(param_);
            param_ = 0;
            hasParam_ = false;
        }
        state_ = St::Ground;
        execCsi(static_cast<char>(b));
    }

    // Parameter (missing → default). 0 is significant (e.g. ED 0), so don't replace it.
    int pnum(std::size_t i, int def) const { return i < params_.size() ? params_[i] : def; }

    void execCsi(char f) {
        const int n = std::max(1, pnum(0, 1));
        switch (f) {
            case 'A': cy_ = std::max(0, cy_ - n); break;
            case 'B': cy_ = std::min(h_ - 1, cy_ + n); break;
            case 'C': cx_ = std::min(w_ - 1, cx_ + n); break;
            case 'D': cx_ = std::max(0, cx_ - n); break;
            case 'E': cy_ = std::min(h_ - 1, cy_ + n); cx_ = 0; break;
            case 'F': cy_ = std::max(0, cy_ - n); cx_ = 0; break;
            case 'G': cx_ = std::clamp(pnum(0, 1) - 1, 0, w_ - 1); break;
            case 'H': case 'f':
                cy_ = std::clamp(pnum(0, 1) - 1, 0, h_ - 1);
                cx_ = std::clamp(pnum(1, 1) - 1, 0, w_ - 1);
                break;
            case 'J': eraseDisplay(pnum(0, 0)); break;
            case 'K': eraseLine(pnum(0, 0)); break;
            case 'L': insertLines(n); break;
            case 'M': deleteLines(n); break;
            case '@': insertChars(n); break;
            case 'P': deleteChars(n); break;
            case 'X': eraseChars(n); break;
            case 'S': for (int i = 0; i < n; ++i) scrollUp_(); break;
            case 'T': for (int i = 0; i < n; ++i) scrollDown_(); break;
            case 'd': cy_ = std::clamp(pnum(0, 1) - 1, 0, h_ - 1); break;
            case 'm': sgr(); break;
            case 'r': // DECSTBM — scrolling region
                if (priv_) break;
                top_ = std::clamp(pnum(0, 1) - 1, 0, h_ - 1);
                bot_ = std::clamp(pnum(1, h_) - 1, top_, h_ - 1);
                cx_ = 0;
                cy_ = top_;
                break;
            case 'h': case 'l': mode(f == 'h'); break;
            default: break; // 'c','n','t', etc. — ignore (no response)
        }
    }

    void mode(bool on) {
        if (!priv_ || params_.empty()) return;
        for (const int v : params_) {
            switch (v) {
                case 1: appCursor_ = on; break;   // DECCKM — application cursor keys
                case 25: cursorVisible_ = on; break; // DECTCEM
                case 47: case 1047: alt(on, false); break;
                case 1049: alt(on, true); break;  // xterm: alt + save cursor
                default: break; // mouse (1000+), bracketed paste (2004) — ignore
            }
        }
    }

    void alt(bool on, bool saveCursor) {
        if (on == altOn_) return;
        if (on) {
            if (saveCursor) { savedX_ = cx_; savedY_ = cy_; }
            altOn_ = true;
            clearBuf();
            cx_ = 0;
            cy_ = 0;
        } else {
            altOn_ = false;
            if (saveCursor) {
                cx_ = std::clamp(savedX_, 0, w_ - 1);
                cy_ = std::clamp(savedY_, 0, h_ - 1);
            }
        }
    }

    void clearBuf() {
        auto& g = buf();
        VtCell c;
        c.bg = bg_;
        std::fill(g.begin(), g.end(), c);
    }

    void put(char32_t c) {
        if (cy_ < 0 || cy_ >= h_) return;
        if (cx_ >= w_) { // deferred wrap simplified to immediate wrap
            cx_ = 0;
            index_();
        }
        VtCell cell;
        cell.ch = c;
        cell.fg = fg_;
        cell.bg = bg_;
        cell.style = style_;
        if (reverse_) cell.style |= kReverse;
        buf()[static_cast<std::size_t>(cy_ * w_ + cx_)] = cell;
        ++cx_;
    }

    void index_() { // LF
        if (cy_ == bot_) scrollUp_();
        else if (cy_ < h_ - 1) ++cy_;
    }

    void reverseIndex_() { // ESC M
        if (cy_ == top_) scrollDown_();
        else if (cy_ > 0) --cy_;
    }

    void scrollUp_() {
        auto& g = buf();
        for (int y = top_; y < bot_; ++y)
            std::move(g.begin() + static_cast<long>((y + 1) * w_),
                      g.begin() + static_cast<long>((y + 2) * w_),
                      g.begin() + static_cast<long>(y * w_));
        blankRow(bot_);
    }

    void scrollDown_() {
        auto& g = buf();
        for (int y = bot_; y > top_; --y)
            std::move(g.begin() + static_cast<long>((y - 1) * w_),
                      g.begin() + static_cast<long>(y * w_),
                      g.begin() + static_cast<long>(y * w_));
        blankRow(top_);
    }

    void blankRow(int y) {
        auto& g = buf();
        VtCell c;
        c.bg = bg_;
        std::fill(g.begin() + static_cast<long>(y * w_),
                  g.begin() + static_cast<long>((y + 1) * w_), c);
    }

    void eraseDisplay(int mode) {
        auto& g = buf();
        VtCell c;
        c.bg = bg_;
        if (mode == 2) {
            std::fill(g.begin(), g.end(), c);
        } else if (mode == 0) {
            for (int x = cx_; x < w_; ++x) g[static_cast<std::size_t>(cy_ * w_ + x)] = c;
            for (int y = cy_ + 1; y < h_; ++y)
                std::fill(g.begin() + static_cast<long>(y * w_),
                          g.begin() + static_cast<long>((y + 1) * w_), c);
        } else if (mode == 1) {
            for (int y = 0; y < cy_; ++y)
                std::fill(g.begin() + static_cast<long>(y * w_),
                          g.begin() + static_cast<long>((y + 1) * w_), c);
            for (int x = 0; x <= cx_ && x < w_; ++x) g[static_cast<std::size_t>(cy_ * w_ + x)] = c;
        }
    }

    void eraseLine(int mode) {
        auto& g = buf();
        VtCell c;
        c.bg = bg_;
        int from = 0, to = w_;
        if (mode == 0) from = cx_;
        else if (mode == 1) to = std::min(w_, cx_ + 1);
        else if (mode != 2) return;
        std::fill(g.begin() + static_cast<long>(cy_ * w_ + from),
                  g.begin() + static_cast<long>(cy_ * w_ + to), c);
    }

    void insertLines(int n) {
        if (cy_ < top_ || cy_ > bot_) return;
        auto& g = buf();
        n = std::min(n, bot_ - cy_ + 1);
        for (int y = bot_; y >= cy_ + n; --y)
            std::move(g.begin() + static_cast<long>((y - n) * w_),
                      g.begin() + static_cast<long>((y - n + 1) * w_),
                      g.begin() + static_cast<long>(y * w_));
        for (int y = cy_; y < cy_ + n; ++y) blankRow(y);
    }

    void deleteLines(int n) {
        if (cy_ < top_ || cy_ > bot_) return;
        auto& g = buf();
        n = std::min(n, bot_ - cy_ + 1);
        for (int y = cy_; y + n <= bot_; ++y)
            std::move(g.begin() + static_cast<long>((y + n) * w_),
                      g.begin() + static_cast<long>((y + n + 1) * w_),
                      g.begin() + static_cast<long>(y * w_));
        for (int y = bot_ - n + 1; y <= bot_; ++y) blankRow(y);
    }

    void insertChars(int n) {
        auto& g = buf();
        n = std::min(n, w_ - cx_);
        auto first = g.begin() + static_cast<long>(cy_ * w_);
        std::move_backward(first + cx_, first + (w_ - n), first + w_);
        for (int x = cx_; x < cx_ + n; ++x) {
            VtCell c;
            c.bg = bg_;
            first[x] = c;
        }
    }

    void deleteChars(int n) {
        auto& g = buf();
        n = std::min(n, w_ - cx_);
        auto first = g.begin() + static_cast<long>(cy_ * w_);
        std::move(first + (cx_ + n), first + w_, first + cx_);
        for (int x = w_ - n; x < w_; ++x) {
            VtCell c;
            c.bg = bg_;
            first[x] = c;
        }
    }

    void eraseChars(int n) {
        auto& g = buf();
        n = std::min(n, w_ - cx_);
        for (int x = cx_; x < cx_ + n; ++x) {
            VtCell c;
            c.bg = bg_;
            g[static_cast<std::size_t>(cy_ * w_ + x)] = c;
        }
    }

    void sgr() {
        if (params_.empty()) { resetSgr(); return; }
        for (std::size_t i = 0; i < params_.size(); ++i) {
            const int v = params_[i];
            if (v == 0) resetSgr();
            else if (v == 1) style_ |= StyleBold;
            else if (v == 2) style_ |= StyleDim;
            else if (v == 4) style_ |= StyleUnderline;
            else if (v == 7) reverse_ = true;
            else if (v == 22) style_ &= static_cast<uint8_t>(~(StyleBold | StyleDim));
            else if (v == 24) style_ &= static_cast<uint8_t>(~StyleUnderline);
            else if (v == 27) reverse_ = false;
            else if (v >= 30 && v <= 37) fg_ = Color{static_cast<uint8_t>(v - 30)};
            else if (v == 39) fg_ = colors::White;
            else if (v >= 90 && v <= 97) fg_ = Color{static_cast<uint8_t>(v - 90 + 8)};
            else if (v >= 40 && v <= 47) bg_ = Color{static_cast<uint8_t>(v - 40)};
            else if (v == 49) bg_ = colors::Black;
            else if (v >= 100 && v <= 107) bg_ = Color{static_cast<uint8_t>(v - 100 + 8)};
            else if (v == 38 || v == 48) {
                Color* target = (v == 38) ? &fg_ : &bg_;
                if (i + 2 < params_.size() && params_[i + 1] == 5) {
                    *target = map256(params_[i + 2]);
                    i += 2;
                } else if (i + 4 < params_.size() && params_[i + 1] == 2) {
                    i += 4; // truecolor — skipped (not supported)
                }
            }
            // 3/5/8/9, etc. — ignore
        }
    }

    void resetSgr() {
        fg_ = colors::White;
        bg_ = colors::Black;
        style_ = StyleNone;
        reverse_ = false;
    }

    static Color map256(int c) {
        c = std::clamp(c, 0, 255);
        if (c < 16) return Color{static_cast<uint8_t>(c)};
        if (c < 232) {
            static const int lv[6] = {0, 95, 135, 175, 215, 255};
            const int v = c - 16;
            return nearest16(lv[v / 36], lv[(v / 6) % 6], lv[v % 6]);
        }
        const int gray = 8 + (c - 232) * 10;
        return nearest16(gray, gray, gray);
    }

    static Color nearest16(int r, int g, int b) {
        static const int pal[16][3] = {
            {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
            {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
            {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
            {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}};
        int best = 0;
        long bd = 1L << 30;
        for (int i = 0; i < 16; ++i) {
            const long dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
            const long d = dr * dr + dg * dg + db * db;
            if (d < bd) { bd = d; best = i; }
        }
        return Color{static_cast<uint8_t>(best)};
    }

    void hardReset() {
        resetSgr();
        cx_ = cy_ = 0;
        savedX_ = savedY_ = 0;
        top_ = 0;
        bot_ = h_ - 1;
        cursorVisible_ = true;
        appCursor_ = false;
        altOn_ = false;
        state_ = St::Ground;
        params_.clear();
        param_ = 0;
        hasParam_ = false;
        priv_ = false;
        grid_.assign(static_cast<std::size_t>(w_ * h_), VtCell{});
        altGrid_.assign(static_cast<std::size_t>(w_ * h_), VtCell{});
    }

    int w_ = 1, h_ = 1, cx_ = 0, cy_ = 0;
    int savedX_ = 0, savedY_ = 0;
    int top_ = 0, bot_ = 0;
    std::vector<VtCell> grid_, altGrid_;
    bool altOn_ = false, cursorVisible_ = true, appCursor_ = false;
    Color fg_ = colors::White, bg_ = colors::Black;
    uint8_t style_ = StyleNone;
    bool reverse_ = false;
    St state_ = St::Ground;
    std::vector<int> params_;
    int param_ = 0;
    bool hasParam_ = false;
    bool priv_ = false;
    static constexpr std::size_t kMaxCsiParams = 16;
    uint32_t utf8Cp_ = 0;
    int utf8Left_ = 0;
};

// --- Pseudoterminal process --------------------------------------------------

class PtyProcess {
public:
    ~PtyProcess() { terminate(); }

    bool spawn(int cols, int rows, std::string& err) {
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
        ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
        pid_ = ::forkpty(&fd_, nullptr, nullptr, &ws);
        if (pid_ < 0) {
            fd_ = -1;
            err = std::strerror(errno);
            return false;
        }
        if (pid_ == 0) { // child: slave is now stdin/stdout/stderr
            ::setenv("TERM", "xterm", 1);
            const char* sh = ::getenv("SHELL");
            if (!sh || !*sh) sh = "/bin/sh";
            ::execl(sh, sh, static_cast<char*>(nullptr));
            ::_exit(127);
        }
        const int fl = ::fcntl(fd_, F_GETFL, 0);
        if (fl < 0 || ::fcntl(fd_, F_SETFL, fl | O_NONBLOCK) < 0) {
            const int e = errno;
            terminate();
            err = std::strerror(e);
            return false;
        }
        return true;
    }

    int fd() const { return fd_; }
    pid_t pid() const { return pid_; }

    void closeFd() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void forgetChild() { pid_ = -1; }

    void resize(int cols, int rows) {
        if (fd_ < 0) return;
        struct winsize ws {};
        ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
        ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
        ::ioctl(fd_, TIOCSWINSZ, &ws); // kernel sends SIGWINCH to the child process group
    }

    // >0: bytes, 0: EAGAIN (no data), -1: EOF/error (slave closed)
    long readSome(char* buf, int cap) {
        ssize_t n;
        do { n = ::read(fd_, buf, static_cast<std::size_t>(cap)); }
        while (n < 0 && errno == EINTR);
        if (n > 0) return static_cast<long>(n);
        if (n == 0) return -1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }

    void writeBytes(const std::string& s) {
        out_.append(s);
        flushOut();
    }

    void flushOut() {
        while (!out_.empty() && fd_ >= 0) {
            const ssize_t n = ::write(fd_, out_.data(), out_.size());
            if (n > 0) {
                out_.erase(0, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) return;
            if (n < 0 && errno == EINTR) continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            out_.clear(); // other error — discard unwritten bytes
            return;
        }
    }

    void terminate() {
        if (pid_ > 0) {
            ::kill(pid_, SIGHUP);
            int st = 0;
            pid_t r;
            do { r = ::waitpid(pid_, &st, WNOHANG); }
            while (r < 0 && errno == EINTR);
            if (r == 0) {
                ::kill(pid_, SIGKILL);
                do { r = ::waitpid(pid_, &st, 0); }
                while (r < 0 && errno == EINTR);
            }
            pid_ = -1;
        }
        closeFd();
    }

private:
    pid_t pid_ = -1;
    int fd_ = -1;
    std::string out_; // pending output — flushed when writable
};

// --- Translate keys to xterm sequences --------------------------------------

inline std::string encodeKey(const KeyEvent& ev, bool appCursor) {
    std::string seq;
    switch (ev.key) {
        case Key::Char: {
            const char32_t c = ev.character;
            if (ev.ctrl && c >= U'A' && c <= U'Z') seq.push_back(static_cast<char>(c - U'A' + 1));
            else if (ev.ctrl && c >= U'a' && c <= U'z') seq.push_back(static_cast<char>(c - U'a' + 1));
            else if (ev.ctrl && c == U' ') seq.push_back(0x00); // Ctrl+Space = NUL
            else if (ev.ctrl && c == U'@') seq.push_back(0x00);
            else if (ev.ctrl && c == U'[') seq.push_back(0x1b); // Ctrl+[ = Esc
            else if (ev.ctrl && c == U'\\') seq.push_back(0x1c);
            else if (ev.ctrl && c == U']') seq.push_back(0x1d);
            else if (ev.ctrl && c == U'^') seq.push_back(0x1e);
            else if (ev.ctrl && c == U'_') seq.push_back(0x1f);
            else if (ev.ctrl && c == U'?') seq.push_back(0x7f); // Ctrl+? = DEL
            else if (c != 0) {
                if (c < 0x80) seq.push_back(static_cast<char>(c));
                else {
                    const std::string u8 = text::toUtf8(std::u32string_view(&c, 1));
                    seq.append(u8);
                }
            }
            break;
        }
        case Key::Enter: seq = "\r"; break;
        case Key::Backspace: seq = "\x7f"; break;
        case Key::Tab: seq = "\t"; break;
        case Key::Escape: seq = "\x1b"; break;
        case Key::Delete: seq = "\x1b[3~"; break;
        case Key::Up: seq = appCursor ? "\x1bOA" : "\x1b[A"; break;
        case Key::Down: seq = appCursor ? "\x1bOB" : "\x1b[B"; break;
        case Key::Right: seq = appCursor ? "\x1bOC" : "\x1b[C"; break;
        case Key::Left: seq = appCursor ? "\x1bOD" : "\x1b[D"; break;
        case Key::Home: seq = appCursor ? "\x1bOH" : "\x1b[H"; break;
        case Key::End: seq = appCursor ? "\x1bOF" : "\x1b[F"; break;
        case Key::PageUp: seq = "\x1b[5~"; break;
        case Key::PageDown: seq = "\x1b[6~"; break;
        case Key::F1: seq = "\x1bOP"; break;
        case Key::F2: seq = "\x1bOQ"; break;
        case Key::F3: seq = "\x1bOR"; break;
        case Key::F4: seq = "\x1bOS"; break;
        case Key::F5: seq = "\x1b[15~"; break;
        case Key::F6: seq = "\x1b[17~"; break;
        case Key::F7: seq = "\x1b[18~"; break;
        case Key::F8: seq = "\x1b[19~"; break;
        case Key::F9: seq = "\x1b[20~"; break;
        case Key::F10: seq = "\x1b[21~"; break;
        case Key::F11: seq = "\x1b[23~"; break;
        case Key::F12: seq = "\x1b[24~"; break;
        default: return {};
    }
    if (ev.alt && !seq.empty()) seq.insert(seq.begin(), '\x1b'); // Meta+X = ESC X
    return seq;
}

// --- Terminal view -----------------------------------------------------------

class TerminalView final : public Widget {
public:
    explicit TerminalView(AppContext& ctx) : ctx_(ctx) {}

    ~TerminalView() override {
        // The fd source must be removed BEFORE closing the master.
        if (pty_.fd() >= 0) ctx_.removeFdSource(pty_.fd());
        pty_.terminate();
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (!spawned_ || exited_) return KeyEventResult::Ignored;
        const std::string bytes = encodeKey(ev, vt_.appCursorKeys());
        if (!bytes.empty()) pty_.writeBytes(bytes);
        return KeyEventResult::Consumed;
    }

    void draw(Screen& screen) override {
        if (bounds_.w <= 0 || bounds_.h <= 0) return;
        if (!spawned_) spawn();
        if (bounds_.w != vt_.width() || bounds_.h != vt_.height()) {
            vt_.resize(bounds_.w, bounds_.h);
            if (pty_.fd() >= 0) pty_.resize(bounds_.w, bounds_.h);
        }
        pump();
        for (int y = 0; y < bounds_.h; ++y) {
            for (int x = 0; x < bounds_.w; ++x) {
                const VtCell c = vt_.at(x, y);
                Color fg = c.fg;
                Color bg = c.bg;
                if (c.style & VtScreen::kReverse) std::swap(fg, bg);
                screen.set(bounds_.x + x, bounds_.y + y,
                           Cell{c.ch, fg, bg,
                                static_cast<uint8_t>(c.style & (StyleBold | StyleUnderline | StyleDim))});
            }
        }
        if (exited_) {
            if (!failMsg_.empty()) {
                std::u32string msg = U" [Failed to spawn shell: " + text::toUtf32(failMsg_) + U"] ";
                screen.putText(bounds_.x, bounds_.y + bounds_.h - 1, msg,
                               colors::Black, colors::Red, StyleBold);
            } else {
                std::u32string msg =
                    U" [Process exited (code " + text::toUtf32(std::to_string(exitCode_)) + U")] ";
                screen.putText(bounds_.x, bounds_.y + bounds_.h - 1, msg,
                               colors::Black, colors::Yellow, StyleBold);
            }
        }
    }

    // Edit: all keys (including arrows and Esc) are forwarded to the shell.
    bool editable() const override { return true; }
    // Nothing to save — Ctrl+W closes without confirmation.
    bool dirty() const override { return false; }
    int minWidth() const override { return 24; }
    int minHeight() const override { return 4; }

    // GUI cursor follows the shell cursor (visible on the output row).
    std::optional<std::pair<int, int>> cursorPosition() const override {
        if (!spawned_ || exited_ || !vt_.cursorVisible() || bounds_.w <= 0 || bounds_.h <= 0)
            return std::nullopt;
        return std::make_pair(bounds_.x + std::clamp(vt_.cursorX(), 0, bounds_.w - 1),
                              bounds_.y + std::clamp(vt_.cursorY(), 0, bounds_.h - 1));
    }

    bool wantsGuiCursor() const override { return spawned_ && !exited_ && vt_.cursorVisible(); }

private:
    void spawn() {
        spawned_ = true;
        vt_.resize(bounds_.w, bounds_.h);
        std::string err;
        if (!pty_.spawn(bounds_.w, bounds_.h, err)) {
            exited_ = true;
            exitCode_ = -1;
            failMsg_ = err;
            return;
        }
        pty_.resize(bounds_.w, bounds_.h);
        ctx_.addFdSource(pty_.fd(), [this] { pump(); });
    }

    void pump() {
        if (pty_.fd() < 0) return;
        pty_.flushOut();
        char buf[4096];
        for (;;) {
            const long n = pty_.readSome(buf, sizeof(buf));
            if (n > 0) {
                vt_.feed(buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) return; // EAGAIN — no data
            // Slave closed — shell has exited; use WNOHANG to avoid hanging
            // if the child remains alive without the slave (daemonization).
            int st = 0;
            pid_t r = 0;
            if (pty_.pid() > 0) {
                do { r = ::waitpid(pty_.pid(), &st, WNOHANG); }
                while (r < 0 && errno == EINTR);
            }
            if (r == pty_.pid()) {
                exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : (WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1);
                pty_.forgetChild();
            } else {
                exitCode_ = -1; // unknown exit code — destructor will clean up the process
            }
            exited_ = true;
            if (pty_.fd() >= 0) ctx_.removeFdSource(pty_.fd());
            pty_.closeFd();
            return;
        }
    }

    AppContext& ctx_;
    PtyProcess pty_;
    VtScreen vt_;
    bool spawned_ = false;
    bool exited_ = false;
    int exitCode_ = 0;
    std::string failMsg_;
};

// --- Application -------------------------------------------------------------

class TerminalApp final : public App {
public:
    [[nodiscard]] std::string name() const override { return "Terminal"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<TerminalView>(ctx);
        addAboutMenu(*view, ctx, U"Terminal",
                     U"Run a shell in a window inside the Fenrir workspace.");
        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        const int w = std::min(72, std::max(24, desktopArea.w - 6));
        const int h = std::min(22, std::max(6, desktopArea.h - 3));
        return Rect{desktopArea.x + 3, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir::apps
