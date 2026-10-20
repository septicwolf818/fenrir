#pragma once
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/Types.hpp"

namespace fenrir {

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
    // Lines of scrollback kept above the visible grid (PgUp/PgDn browses
    // them; any new output or other key returns to the live view).
    static constexpr std::size_t kMaxHistory = 1000;

    bool inAltScreen() const { return altOn_; }
    // Scrollback content: the number of logical lines kept above the screen.
    std::size_t historySize() const { return history_.size(); }
    // Scrollback rows at the current width (the lines are re-wrapped for
    // display, so a wide line can take several rows).
    std::size_t historyRowCount() const { return historyRows().size(); }
    int viewOffset() const { return view_; }

    // Browse the scrollback (positive = older). Clamped to [0, history].
    void scrollView(int lines) {
        view_ = std::clamp(view_ + lines, 0, static_cast<int>(historyRows().size()));
    }

    // Return to the live view (called on new output and on most keys).
    void follow() { view_ = 0; }

    void resize(int w, int h) {
        const int nw = std::max(1, w);
        const int nh = std::max(1, h);
        if (grid_.empty() && altGrid_.empty()) {
            // First allocation — no content to preserve.
            w_ = nw;
            h_ = nh;
            grid_.assign(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh), VtCell{});
            altGrid_.assign(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh), VtCell{});
            gridWrap_.assign(static_cast<std::size_t>(nh), 0);
            altWrap_.assign(static_cast<std::size_t>(nh), 0);
            cx_ = 0;
            cy_ = 0;
            top_ = 0;
            bot_ = nh - 1;
            return;
        }
        // Reflow both buffers and map each buffer's cursor through its own
        // layout. While the alternate screen is active, savedX_/savedY_ is
        // the normal-screen cursor saved by 1049; cx_/cy_ belongs to altGrid_.
        int newCx = cx_, newCy = cy_;
        int newSavedX = savedX_, newSavedY = savedY_;
        if (altOn_) {
            int normalX = savedX_, normalY = savedY_;
            int ignoredX = 0, ignoredY = 0;
            reflowInto(grid_, gridWrap_, w_, h_, nw, nh, savedX_, savedY_, savedX_, savedY_,
                       normalX, normalY, ignoredX, ignoredY);
            reflowInto(altGrid_, altWrap_, w_, h_, nw, nh, cx_, cy_, cx_, cy_,
                       newCx, newCy, ignoredX, ignoredY);
            newSavedX = normalX;
            newSavedY = normalY;
        } else {
            int ignoredX = 0, ignoredY = 0;
            reflowInto(grid_, gridWrap_, w_, h_, nw, nh, cx_, cy_, savedX_, savedY_,
                       newCx, newCy, newSavedX, newSavedY);
            reflowInto(altGrid_, altWrap_, w_, h_, nw, nh, 0, 0, 0, 0,
                       ignoredX, ignoredY, ignoredX, ignoredY);
        }
        // The scrollback holds logical lines, so a resize only changes how
        // they are wrapped for display — the text itself is never rewritten.
        w_ = nw;
        h_ = nh;
        historyRowsDirty_ = true;
        view_ = std::clamp(view_, 0, static_cast<int>(historyRows().size()));
        // cx_ == width means the last cell was written and the next printable
        // character wraps. Preserve that state across reflow; clamping it to
        // width-1 makes a subsequent character overwrite the last cell.
        cx_ = (newCx == w_) ? w_ : std::clamp(newCx, 0, w_ - 1);
        cy_ = std::clamp(newCy, 0, h_ - 1);
        savedX_ = (newSavedX == w_) ? w_ : std::clamp(newSavedX, 0, w_ - 1);
        savedY_ = std::clamp(newSavedY, 0, h_ - 1);
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
        if (view_ > 0 && !altOn_) {
            // Scrolled back: the visible rows are the tail of the combined
            // stream (scrollback rows re-wrapped to the current width, then
            // the live grid), shifted up by view_ rows.
            const auto& hist = historyRows();
            const int histCount = static_cast<int>(hist.size());
            const int row = histCount - view_ + y;
            if (row < 0) return blank;
            if (row < histCount) {
                const auto& hr = hist[static_cast<std::size_t>(row)];
                if (x >= static_cast<int>(hr.size())) return blank;
                return hr[static_cast<std::size_t>(x)];
            }
            const std::size_t idx =
                static_cast<std::size_t>(row - histCount) * static_cast<std::size_t>(w_) +
                static_cast<std::size_t>(x);
            if (idx >= grid_.size()) return blank;
            return grid_[idx];
        }
        const auto& b = buf();
        const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) +
                                static_cast<std::size_t>(x);
        if (idx >= b.size()) return blank;
        return b[idx];
    }

    void feed(const char* data, std::size_t n) {
        if (n > 0) view_ = 0; // new output returns to the live view
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
    std::vector<uint8_t>& wraps() { return altOn_ ? altWrap_ : gridWrap_; }

    // Scrollback rows at the current width. The history itself stores logical
    // lines (the text between newlines, with soft-wrapped rows joined), so a
    // width change only re-wraps the text for display — nothing is truncated.
    const std::vector<std::vector<VtCell>>& historyRows() const {
        if (!historyRowsDirty_) return historyRowsCache_;
        historyRowsCache_.clear();
        const std::size_t width = static_cast<std::size_t>(w_);
        for (const auto& line : history_) {
            if (line.empty()) {
                historyRowsCache_.emplace_back();
                continue;
            }
            for (std::size_t off = 0; off < line.size(); off += width) {
                const std::size_t end = std::min(line.size(), off + width);
                historyRowsCache_.emplace_back(line.begin() + static_cast<long>(off),
                                               line.begin() + static_cast<long>(end));
            }
        }
        historyRowsDirty_ = false;
        return historyRowsCache_;
    }

    // Removes the padding blanks at the end of a completed history line.
    static void trimTrailingBlanks(std::vector<VtCell>& line) {
        while (!line.empty() && line.back().ch == U' ')
            line.pop_back();
    }

    // Reflow one cell buffer from oldW×oldH to newW×newH, preserving visible
    // text by re-wrapping logical lines using explicit soft-wrap markers.
    // The cursor (curX/curY, possibly curX == oldW for pending wrap) is mapped
    // to the same logical offset in the new layout. Overflow keeps the last
    // newH rows (prompt stays visible); underflow pads with blanks at the
    // bottom. New blanks use VtCell{} (same as a fresh grid).
    static void reflowInto(std::vector<VtCell>& cells, std::vector<uint8_t>& wrap,
                           int oldW, int oldH,
                           int newW, int newH, int curX, int curY,
                           int savedX, int savedY,
                           int& outX, int& outY, int& outSavedX, int& outSavedY) {
        const std::size_t expect =
            static_cast<std::size_t>(std::max(0, oldW)) * static_cast<std::size_t>(std::max(0, oldH));
        if (cells.size() != expect || wrap.size() != static_cast<std::size_t>(oldH) ||
            oldW <= 0 || oldH <= 0) {
            cells.assign(static_cast<std::size_t>(newW) * static_cast<std::size_t>(newH), VtCell{});
            wrap.assign(static_cast<std::size_t>(newH), 0);
            outX = 0;
            outY = 0;
            outSavedX = 0;
            outSavedY = 0;
            return;
        }
        // 1. Split into logical lines.
        std::vector<std::vector<VtCell>> logical;
        logical.reserve(static_cast<std::size_t>(oldH));
        std::vector<int> rowLogic(static_cast<std::size_t>(oldH), 0);
        std::vector<std::size_t> rowBase(static_cast<std::size_t>(oldH), 0);
        std::vector<VtCell> cur;
        const int mapCurY = std::clamp(curY, 0, oldH - 1);
        const int mapSavedY = std::clamp(savedY, 0, oldH - 1);
        const int mapCurX = std::clamp(curX, 0, oldW);
        const int mapSavedX = std::clamp(savedX, 0, oldW);
        for (int y = 0; y < oldH; ++y) {
            int last = -1;
            const std::size_t base = static_cast<std::size_t>(y) * static_cast<std::size_t>(oldW);
            for (int x = oldW - 1; x >= 0; --x) {
                if (cells[base + static_cast<std::size_t>(x)].ch != U' ') { last = x; break; }
            }
            const int rowLen = last + 1; // 0 = blank row
            const bool wrapped = wrap[static_cast<std::size_t>(y)] != 0;
            rowLogic[static_cast<std::size_t>(y)] = static_cast<int>(logical.size());
            rowBase[static_cast<std::size_t>(y)] = cur.size();
            int preserve = wrapped ? oldW : rowLen;
            if (y == mapCurY) preserve = std::max(preserve, mapCurX);
            if (y == mapSavedY) preserve = std::max(preserve, mapSavedX);
            for (int x = 0; x < preserve; ++x)
                cur.push_back(cells[base + static_cast<std::size_t>(x)]);
            if (!wrapped) {
                logical.push_back(std::move(cur));
                cur.clear();
            }
        }
        if (!cur.empty()) logical.push_back(std::move(cur));
        // Drop trailing blank logical lines beyond both cursor anchors: they
        // are screen padding and must not displace content on height shrink.
        // Keep blank rows up through a cursor so blank-screen cursor placement
        // survives a resize. Padding is re-added below when space permits.
        const int lastCursorLine = std::max(rowLogic[static_cast<std::size_t>(mapCurY)],
                                            rowLogic[static_cast<std::size_t>(mapSavedY)]);
        while (static_cast<int>(logical.size()) - 1 > lastCursorLine &&
               !logical.empty() && logical.back().empty())
            logical.pop_back();

        // 2. Map the cursor to (logical line, offset in line).
        int logicIdx = 0;
        std::size_t logicOff = 0;
        if (!logical.empty()) {
            logicIdx = rowLogic[static_cast<std::size_t>(mapCurY)];
            logicIdx = std::clamp(logicIdx, 0, static_cast<int>(logical.size()) - 1);
            const std::size_t base = rowBase[static_cast<std::size_t>(mapCurY)];
            logicOff = base + static_cast<std::size_t>(mapCurX);
            logicOff = std::min(logicOff, logical[static_cast<std::size_t>(logicIdx)].size());
        }

        int savedLogicIdx = 0;
        std::size_t savedLogicOff = 0;
        if (!logical.empty()) {
            savedLogicIdx = std::clamp(rowLogic[static_cast<std::size_t>(mapSavedY)],
                                       0, static_cast<int>(logical.size()) - 1);
            savedLogicOff = rowBase[static_cast<std::size_t>(mapSavedY)] +
                            static_cast<std::size_t>(mapSavedX);
            savedLogicOff = std::min(savedLogicOff,
                                     logical[static_cast<std::size_t>(savedLogicIdx)].size());
        }

        // 3. Re-wrap to the new width.
        std::vector<std::vector<VtCell>> phys;
        std::vector<uint8_t> physWrap;
        phys.reserve(logical.size());
        physWrap.reserve(logical.size());
        std::vector<int> logicStart(logical.size(), 0);
        for (std::size_t li = 0; li < logical.size(); ++li) {
            logicStart[li] = static_cast<int>(phys.size());
            const auto& line = logical[li];
            if (line.empty()) {
                phys.emplace_back();
                physWrap.push_back(0);
            } else {
                for (std::size_t off = 0; off < line.size(); off += static_cast<std::size_t>(newW)) {
                    phys.push_back(std::vector<VtCell>(line.begin() + static_cast<long>(off),
                                                      line.begin() +
                                                          static_cast<long>(std::min(
                                                              line.size(),
                                                              off + static_cast<std::size_t>(newW)))));
                    physWrap.push_back(off + static_cast<std::size_t>(newW) < line.size() ? 1 : 0);
                }
            }
        }

        // 4. Fit to the new height: keep the last newH rows (bottom-anchored),
        // otherwise pad with blanks at the bottom (top-anchored growth).
        int drop = static_cast<int>(phys.size()) - newH;
        if (drop < 0) drop = 0;
        const int kept = static_cast<int>(phys.size()) - drop;

        cells.assign(static_cast<std::size_t>(newW) * static_cast<std::size_t>(newH), VtCell{});
        wrap.assign(static_cast<std::size_t>(newH), 0);
        for (int r = 0; r < kept; ++r) {
            const std::size_t srcRow = static_cast<std::size_t>(r + drop);
            const auto& row = phys[srcRow];
            const std::size_t dst = static_cast<std::size_t>(r) * static_cast<std::size_t>(newW);
            for (std::size_t x = 0; x < row.size(); ++x) cells[dst + x] = row[x];
            wrap[static_cast<std::size_t>(r)] = physWrap[srcRow];
        }

        if (logical.empty()) {
            outX = 0;
            outY = 0;
            outSavedX = 0;
            outSavedY = 0;
            return;
        }
        auto mapPoint = [&](int line, std::size_t offset, bool pendingWrap, int& x, int& y) {
            const int startRow = logicStart[static_cast<std::size_t>(line)] - drop;
            const std::size_t width = static_cast<std::size_t>(newW);
            const bool exactBoundary = pendingWrap && offset > 0 && offset % width == 0;
            const int row = startRow + static_cast<int>(
                exactBoundary ? (offset / width) - 1 : offset / width);
            x = exactBoundary ? newW : static_cast<int>(offset % width);
            y = std::clamp(row, 0, newH - 1);
            if (row < 0) x = 0; // cursor scrolled off the top — park at origin
        };
        mapPoint(logicIdx, logicOff, curX == oldW, outX, outY);
        mapPoint(savedLogicIdx, savedLogicOff, savedX == oldW, outSavedX, outSavedY);
    }

    void ground(unsigned char b) {
        switch (b) {
            case 0x1b: state_ = St::Esc; return;
            case '\r': cx_ = 0; return;
            case '\n':
            case 0x0b:
            case 0x0c:
                cx_ = std::min(cx_, w_ - 1);
                if (wraps().size() > static_cast<std::size_t>(cy_))
                    wraps()[static_cast<std::size_t>(cy_)] = 0;
                index_();
                return;
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
            case '8':
                cx_ = (savedX_ == w_) ? w_ : std::clamp(savedX_, 0, w_ - 1);
                cy_ = std::clamp(savedY_, 0, h_ - 1);
                break; // DECRC
            case 'M': reverseIndex_(); break; // RI
            case 'D':
                cx_ = std::min(cx_, w_ - 1);
                if (wraps().size() > static_cast<std::size_t>(cy_))
                    wraps()[static_cast<std::size_t>(cy_)] = 0;
                index_();
                break; // IND
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
        view_ = 0; // the alternate screen has no scrollback view
        if (on) {
            if (saveCursor) { savedX_ = cx_; savedY_ = cy_; }
            altOn_ = true;
            clearBuf();
            cx_ = 0;
            cy_ = 0;
        } else {
            altOn_ = false;
            if (saveCursor) {
                cx_ = (savedX_ == w_) ? w_ : std::clamp(savedX_, 0, w_ - 1);
                cy_ = std::clamp(savedY_, 0, h_ - 1);
            }
        }
    }

    void clearBuf() {
        auto& g = buf();
        auto& rowWraps = wraps();
        VtCell c;
        c.bg = bg_;
        std::fill(g.begin(), g.end(), c);
        std::fill(rowWraps.begin(), rowWraps.end(), 0);
    }

    void put(char32_t c) {
        if (cy_ < 0 || cy_ >= h_ || cx_ < 0) return;
        auto& rowWraps = wraps();
        if (rowWraps.size() != static_cast<std::size_t>(h_)) return;
        if (cx_ >= w_) { // deferred wrap simplified to immediate wrap
            rowWraps[static_cast<std::size_t>(cy_)] = 1;
            cx_ = 0;
            index_();
        }
        rowWraps[static_cast<std::size_t>(cy_)] = 0;
        auto& b = buf();
        const std::size_t idx = static_cast<std::size_t>(cy_) * static_cast<std::size_t>(w_) +
                                static_cast<std::size_t>(cx_);
        if (idx >= b.size()) return;
        VtCell cell;
        cell.ch = c;
        cell.fg = fg_;
        cell.bg = bg_;
        cell.style = style_;
        if (reverse_) cell.style |= kReverse;
        b[idx] = cell;
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
        auto& rowWraps = wraps();
        if (!altOn_ && top_ == 0 && bot_ == h_ - 1 && h_ > 0 &&
            g.size() == static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_)) {
            // The top row of the normal screen scrolls off — keep it for PgUp.
            // The row continues the line above it when that line was
            // soft-wrapped, so it joins the open history line instead of
            // starting a new one. The line stays open while its remaining
            // part is still on the screen.
            const auto first = g.begin();
            const auto last = g.begin() + w_;
            if (historyOpen_ && !history_.empty())
                history_.back().insert(history_.back().end(), first, last);
            else
                history_.emplace_back(first, last);
            // The row padding at the end of the line is not text — dropping it
            // keeps the line free of gaps when it is re-wrapped later. The
            // line stays open while its remaining rows are still on screen.
            trimTrailingBlanks(history_.back());
            historyOpen_ = rowWraps[0] != 0;
            while (history_.size() > kMaxHistory) {
                history_.pop_front();
                if (history_.empty()) historyOpen_ = false;
            }
            historyRowsDirty_ = true;
        }
        for (int y = top_; y < bot_; ++y)
            std::move(g.begin() + static_cast<long>((y + 1) * w_),
                      g.begin() + static_cast<long>((y + 2) * w_),
                      g.begin() + static_cast<long>(y * w_));
        for (int y = top_; y < bot_; ++y)
            rowWraps[static_cast<std::size_t>(y)] = rowWraps[static_cast<std::size_t>(y + 1)];
        blankRow(bot_);
    }

    void scrollDown_() {
        auto& g = buf();
        auto& rowWraps = wraps();
        for (int y = bot_; y > top_; --y)
            std::move(g.begin() + static_cast<long>((y - 1) * w_),
                      g.begin() + static_cast<long>(y * w_),
                      g.begin() + static_cast<long>(y * w_));
        for (int y = bot_; y > top_; --y)
            rowWraps[static_cast<std::size_t>(y)] = rowWraps[static_cast<std::size_t>(y - 1)];
        blankRow(top_);
    }

    void blankRow(int y) {
        auto& g = buf();
        wraps()[static_cast<std::size_t>(y)] = 0;
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
            std::fill(wraps().begin(), wraps().end(), 0);
        } else if (mode == 0) {
            for (int x = cx_; x < w_; ++x) g[static_cast<std::size_t>(cy_ * w_ + x)] = c;
            for (int y = cy_; y < h_; ++y) wraps()[static_cast<std::size_t>(y)] = 0;
            for (int y = cy_ + 1; y < h_; ++y)
                std::fill(g.begin() + static_cast<long>(y * w_),
                          g.begin() + static_cast<long>((y + 1) * w_), c);
        } else if (mode == 1) {
            for (int y = 0; y < cy_; ++y)
                std::fill(g.begin() + static_cast<long>(y * w_),
                          g.begin() + static_cast<long>((y + 1) * w_), c);
            for (int y = 0; y <= cy_; ++y) wraps()[static_cast<std::size_t>(y)] = 0;
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
        wraps()[static_cast<std::size_t>(cy_)] = 0;
    }

    void insertLines(int n) {
        if (cy_ < top_ || cy_ > bot_) return;
        auto& g = buf();
        n = std::min(n, bot_ - cy_ + 1);
        for (int y = bot_; y >= cy_ + n; --y)
            std::move(g.begin() + static_cast<long>((y - n) * w_),
                      g.begin() + static_cast<long>((y - n + 1) * w_),
                      g.begin() + static_cast<long>(y * w_));
        auto& rowWraps = wraps();
        for (int y = bot_; y >= cy_ + n; --y)
            rowWraps[static_cast<std::size_t>(y)] = rowWraps[static_cast<std::size_t>(y - n)];
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
        auto& rowWraps = wraps();
        for (int y = cy_; y + n <= bot_; ++y)
            rowWraps[static_cast<std::size_t>(y)] = rowWraps[static_cast<std::size_t>(y + n)];
        for (int y = bot_ - n + 1; y <= bot_; ++y) blankRow(y);
    }

    void insertChars(int n) {
        auto& g = buf();
        wraps()[static_cast<std::size_t>(cy_)] = 0;
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
        wraps()[static_cast<std::size_t>(cy_)] = 0;
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
        wraps()[static_cast<std::size_t>(cy_)] = 0;
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
        gridWrap_.assign(static_cast<std::size_t>(h_), 0);
        altWrap_.assign(static_cast<std::size_t>(h_), 0);
        history_.clear();
        historyOpen_ = false;
        historyRowsDirty_ = true;
        view_ = 0;
    }

    int w_ = 1, h_ = 1, cx_ = 0, cy_ = 0;
    int savedX_ = 0, savedY_ = 0;
    int top_ = 0, bot_ = 0;
    std::vector<VtCell> grid_, altGrid_;
    std::vector<uint8_t> gridWrap_, altWrap_;
    // Scrollback as logical lines: one entry per newline-terminated line,
    // with soft-wrapped rows joined into the line they continue.
    std::deque<std::vector<VtCell>> history_;
    // The last history line continues on the screen (its remaining rows have
    // not scrolled off yet).
    bool historyOpen_ = false;
    // Scrollback rows re-wrapped to the current width (display cache).
    mutable std::vector<std::vector<VtCell>> historyRowsCache_;
    mutable bool historyRowsDirty_ = true;
    int view_ = 0; // scrolled-back lines (0 = live)
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

} // namespace fenrir
