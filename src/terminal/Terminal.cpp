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

// Sygnały (SIGWINCH/SIGINT/SIGTERM) mogą przerwać program w dowolnym
// miejscu, więc handler nie może bezpiecznie wołać metod obiektu C++.
// Zamiast tego trzymamy globalny wskaźnik na JEDYNĄ instancję Terminal
// (framework zakłada jeden terminal na proces) i w handlerze robimy
// tylko rzeczy async-signal-safe: ustawienie flagi albo bezpośredni
// zapis surowych sekwencji + _exit.
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

    // SIGINT/SIGTERM: przywróć terminal do normalnego stanu i dopiero
    // wtedy zakończ proces — inaczej użytkownik zostaje z "zepsutym"
    // terminalem (bez echa, w alternate screen, z ukrytym kursorem).
    static void onTerminatingSignal(int signum) {
        if (Impl* self = g_activeImpl.load()) {
            self->emergencyRestore();
        }
        std::signal(signum, SIG_DFL);
        std::raise(signum);
    }

    // Wersja "awaryjna" restore() — tylko wywołania async-signal-safe
    // (write/tcsetattr/ioctl), bez alokacji, bez wyjątków, bez iostream.
    void emergencyRestore() {
        if (alternateScreenActive) {
            static constexpr char showCursor[] = "\x1b[?25h";
            static constexpr char leaveAlt[] = "\x1b[?1049l";
            ::write(STDOUT_FILENO, showCursor, sizeof(showCursor) - 1);
            ::write(STDOUT_FILENO, leaveAlt, sizeof(leaveAlt) - 1);
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
        throw std::runtime_error("fenrir::Terminal wymaga prawdziwego TTY na stdin/stdout");
    }

    g_activeImpl.store(impl_);

    // Konstruktor nie ma destruktora — jesli rzucimy w polowie (np. write w
    // enterAlternateScreen), ~Terminal() NIE zostanie wywolany, a uzytkownik
    // zostalby z wylaczonym echo i ekranem alternatywnym. Cofamy wiec recznie
    // wszystko, co zdazylismy wlaczyc, i dopiero wtedy wyrzucamy blad.
    try {
        enableRawMode();
        enterAlternateScreen();
        hideSystemCursor();
        installSignalHandlers();
    } catch (...) {
        // Poszczegolne kroki moga sie tez nie udac (write do zepsutego
        // stdout) — ignorujemy, zeby nie zgubic oryginalnego bledu ani
        // samego impl_ (wyciek fd/termios bylby gorszy od pierwotnego problemu).
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
        throw std::runtime_error("tcgetattr nie powiodło się");
    }

    termios raw = impl_->originalTermios;

    // Wyłączamy: echo, tryb kanoniczny (linia po linii), sygnały z
    // klawiatury (Ctrl+C/Ctrl+Z jako SIGINT/SIGTSTP), rozszerzone
    // przetwarzanie wejścia — chcemy widzieć KAŻDY bajt tak, jak
    // przyszedł, sami decydujemy co z nim zrobić.
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    // Wyłączamy przetwarzanie wejścia typu XON/XOFF, konwersję CR->NL itd.
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);

    // Wyłączamy przetwarzanie wyjścia (np. automatyczne \n -> \r\n) —
    // renderer sam w pełni kontroluje to, co wysyła.
    raw.c_oflag &= ~(OPOST);

    raw.c_cflag |= CS8;

    // read() ma zwracać natychmiast, gdy jest choć 1 bajt; blokowanie z
    // timeoutem realizujemy sami przez poll() w Terminal::read().
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        throw std::runtime_error("tcsetattr (raw mode) nie powiodło się");
    }
    impl_->rawModeActive = true;
}

void Terminal::disableRawMode() {
    if (!impl_->rawModeActive) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &impl_->originalTermios);
    impl_->rawModeActive = false;
}

void Terminal::enterAlternateScreen() {
    // 1049: zachowuje zawartość "normalnego" ekranu i przełącza na
    // czysty bufor alternatywny — po wyjściu użytkownik wraca do tego,
    // co miał w terminalu przed uruchomieniem programu.
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
    // Robimy to RAZ, na starcie, i nie włączamy z powrotem aż do wyjścia
    // z programu. Systemowy kursor terminala nigdy nie jest używany jako
    // kursor GUI — o kursorze GUI decyduje wyłącznie nasz Renderer.
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
    // Rozsądny fallback, gdyby ioctl zawiódł (np. wyjście przekierowane).
    return TerminalSize{80, 24};
}

void Terminal::write(std::string_view data) {
    std::size_t written = 0;
    while (written < data.size()) {
        ssize_t n = ::write(STDOUT_FILENO, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("write() na stdout nie powiodło się");
        }
        written += static_cast<std::size_t>(n);
    }
}

void Terminal::flush() {
    // Piszemy bezpośrednio przez write(2) (bez buforowania stdio), więc
    // nie ma tu nic do "zrzucenia" — funkcja istnieje, żeby wyższe
    // warstwy (Renderer) miały jasny punkt synchronizacji i żeby można
    // było później podmienić backend (np. na buforowany) bez zmiany API.
}

std::size_t Terminal::read(char* buf, std::size_t maxLen, int timeoutMs) {
    if (timeoutMs >= 0) {
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        int rc = poll(&pfd, 1, timeoutMs);
        if (rc <= 0) return 0; // timeout albo błąd -> brak danych
    }

    ssize_t n = ::read(STDIN_FILENO, buf, maxLen);
    if (n < 0) {
        if (errno == EINTR) return 0;
        throw std::runtime_error("read() ze stdin nie powiodło się");
    }
    return static_cast<std::size_t>(n);
}

bool Terminal::consumeResizeFlag() {
    return impl_->resized.exchange(false);
}

} // namespace fenrir
