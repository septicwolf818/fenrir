#include "fenrir/terminal/Terminal.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace fenrir {

namespace {

// Signals (SIGWINCH/SIGINT/SIGTERM) can interrupt the program anywhere,
// so the handler cannot safely call methods on the C++ object.
// Instead we keep a global pointer to the ONE Terminal instance
// (the framework assumes one terminal per process) and in the handler do
// only async-signal-safe things: set a flag or directly
// write raw sequences + _exit.
std::atomic<Terminal::Impl*> g_activeImpl{nullptr};

} // namespace

struct Terminal::Impl {
    termios originalTermios{};
    bool rawModeActive = false;
    bool alternateScreenActive = false;
    std::atomic<bool> resized{false};

    static void onResize(int /*signum*/) {
        if (Impl* self = g_activeImpl.load()) {
            self->resized.store(true);
        }
    }

    // SIGINT/SIGTERM: restore the terminal to its normal state and only
    // then end the process — otherwise the user is left with a "broken"
    // terminal (no echo, in the alternate screen, with a hidden cursor).
    static void onTerminatingSignal(int signum) {
        if (Impl* self = g_activeImpl.load()) {
            self->emergencyRestore();
        }
        std::signal(signum, SIG_DFL);
        std::raise(signum);
    }

    // "Emergency" version of restore() — only async-signal-safe
    // calls (write/tcsetattr/ioctl), no allocation, no exceptions, no iostream.
    void emergencyRestore() {
        if (alternateScreenActive) {
            static constexpr char showCursor[] = "\x1b[?25h";
            static constexpr char leaveAlt[] = "\x1b[?1049l";
            // Result deliberately ignored (emergency mode, async-signal-safe);
            // the assignment silences warn_unused_result on glibc.
            [[maybe_unused]] const ssize_t r1 = ::write(STDOUT_FILENO, showCursor, sizeof(showCursor) - 1);
            [[maybe_unused]] const ssize_t r2 = ::write(STDOUT_FILENO, leaveAlt, sizeof(leaveAlt) - 1);
        }
        if (rawModeActive) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &originalTermios);
        }
    }
};

Terminal::Terminal() : impl_(new Impl()) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        delete impl_;
        impl_ = nullptr;
        throw std::runtime_error("fenrir::Terminal requires a real TTY on stdin/stdout");
    }

    g_activeImpl.store(impl_);

    // The constructor has no destructor — if we throw midway (e.g. write in
    // enterAlternateScreen), ~Terminal() will NOT be called, and the user
    // would be left with echo disabled and an alternate screen. So we manually
    // roll back everything we managed to enable, and only then throw.
    try {
        enableRawMode();
        enterAlternateScreen();
        hideSystemCursor();
        installSignalHandlers();
    } catch (...) {
        // The individual steps can fail too (write to a broken
        // stdout) — we ignore that so we don't lose the original error or
        // the impl_ itself (an fd/termios leak would be worse than the original problem).
        try { restoreSignalHandlers(); } catch (...) {}
        try { showSystemCursor(); } catch (...) {}
        try { leaveAlternateScreen(); } catch (...) {}
        try { disableRawMode(); } catch (...) {}
        g_activeImpl.store(nullptr);
        delete impl_;
        impl_ = nullptr;
        throw;
    }
}

Terminal::~Terminal() {
    if (!impl_) return;

    restoreSignalHandlers();
    showSystemCursor();
    leaveAlternateScreen();
    disableRawMode();

    g_activeImpl.store(nullptr);
    delete impl_;
}

void Terminal::enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &impl_->originalTermios) != 0) {
        throw std::runtime_error("tcgetattr failed");
    }

    termios raw = impl_->originalTermios;

    // We disable: echo, canonical mode (line by line), keyboard
    // signals (Ctrl+C/Ctrl+Z as SIGINT/SIGTSTP), extended
    // input processing — we want to see EVERY byte exactly as it
    // arrived; we decide ourselves what to do with it.
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    // We disable XON/XOFF input processing, CR->NL conversion etc.
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

    // We disable output processing (e.g. automatic \n -> \r\n) —
    // the renderer fully controls what it sends.
    raw.c_oflag &= ~(OPOST);

    raw.c_cflag |= CS8;

    // read() should return as soon as there is at least 1 byte; timeout
    // blocking is implemented by us via poll() in Terminal::read().
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        throw std::runtime_error("tcsetattr (raw mode) failed");
    }
    impl_->rawModeActive = true;
}

void Terminal::disableRawMode() {
    if (!impl_->rawModeActive) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &impl_->originalTermios);
    impl_->rawModeActive = false;
}

void Terminal::enterAlternateScreen() {
    // 1049: preserves the content of the "normal" screen and switches to
    // a clean alternate buffer — on exit the user returns to what
    // they had in the terminal before starting the program.
    write("\x1b[?1049h");
    flush();
    impl_->alternateScreenActive = true;
}

void Terminal::leaveAlternateScreen() {
    if (!impl_->alternateScreenActive) return;
    write("\x1b[?1049l");
    flush();
    impl_->alternateScreenActive = false;
}

void Terminal::hideSystemCursor() {
    // We do this ONCE, at startup, and never turn it back on until we exit
    // the program. The terminal's system cursor is never used as the
    // GUI cursor — the GUI cursor is decided solely by our Renderer.
    write("\x1b[?25l");
    flush();
}

void Terminal::showSystemCursor() {
    write("\x1b[?25h");
    flush();
}

void Terminal::installSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = &Impl::onResize;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    struct sigaction sa2{};
    sa2.sa_handler = &Impl::onTerminatingSignal;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGINT, &sa2, nullptr);
    sigaction(SIGTERM, &sa2, nullptr);
}

void Terminal::restoreSignalHandlers() {
    std::signal(SIGWINCH, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);
}

TerminalSize Terminal::size() const {
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        return TerminalSize{ws.ws_col, ws.ws_row};
    }
    // Reasonable fallback if ioctl fails (e.g. output redirected).
    return TerminalSize{80, 24};
}

void Terminal::write(std::string_view data) {
    std::size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(STDOUT_FILENO, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("write() to stdout failed");
        }
        written += static_cast<std::size_t>(n);
    }
}

void Terminal::flush() {
    // We write directly through write(2) (no stdio buffering), so
    // there is nothing to "flush" here — the function exists so higher
    // layers (Renderer) have an explicit synchronization point and so the
    // backend can be swapped later (e.g. for a buffered one) without API changes.
}

std::size_t Terminal::read(char* buf, std::size_t maxLen, int timeoutMs) {
    if (timeoutMs >= 0) {
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        int rc = poll(&pfd, 1, timeoutMs);
        if (rc <= 0) return 0; // timeout or error -> no data
    }

    ssize_t n = ::read(STDIN_FILENO, buf, maxLen);
    if (n < 0) {
        if (errno == EINTR) return 0;
        throw std::runtime_error("read() from stdin failed");
    }
    return static_cast<std::size_t>(n);
}

bool Terminal::consumeResizeFlag() {
    return impl_->resized.exchange(false);
}

} // namespace fenrir
