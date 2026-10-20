#pragma once
// Terminal — basic terminal emulator in a Fenrir window.
// Runs a real shell ($SHELL or /bin/sh) in a pseudoterminal (forkpty)
// and draws its output in a cell grid. Supported VT100/xterm subset:
// cursor positioning, line clearing/editing, scrolling regions,
// alternate screen (1049), application cursor mode (h/l), SGR 16/256 colors,
// reverse video, UTF-8. Key translation to xterm sequences.
// PgUp/PgDn browse the scrollback on the normal screen (new output or any
// other key returns to the live view); on the alternate screen they are
// forwarded so fullscreen apps (less, vim, ...) keep working.
// Alt+Tab switches windows; Esc is forwarded to the shell (an exception to
// the general Esc reservation rule, documented in this app).
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
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
#include "fenrir/terminal/PtyProcess.hpp"
#include "fenrir/terminal/VtScreen.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

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
        // PgUp/PgDn browse the scrollback on the normal screen (where the
        // shell has no use for them); fullscreen apps on the alternate
        // screen (less, vim, ...) still receive them.
        if ((ev.key == Key::PageUp || ev.key == Key::PageDown) && !vt_.inAltScreen()) {
            const int page = std::max(1, vt_.height() - 1);
            vt_.scrollView(ev.key == Key::PageUp ? page : -page);
            return KeyEventResult::Consumed;
        }
        vt_.follow(); // any other key returns to the live view
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
    // Hidden while browsing the scrollback — the shell cursor is not there.
    std::optional<std::pair<int, int>> cursorPosition() const override {
        if (!spawned_ || exited_ || !vt_.cursorVisible() || vt_.viewOffset() > 0 ||
            bounds_.w <= 0 || bounds_.h <= 0)
            return std::nullopt;
        return std::make_pair(bounds_.x + std::clamp(vt_.cursorX(), 0, bounds_.w - 1),
                              bounds_.y + std::clamp(vt_.cursorY(), 0, bounds_.h - 1));
    }

    bool wantsGuiCursor() const override {
        return spawned_ && !exited_ && vt_.cursorVisible() && vt_.viewOffset() == 0;
    }

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
        addExitMenu(*view, ctx);
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
