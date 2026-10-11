#pragma once

#include <cstddef>
#include <string_view>

namespace fenrir {

struct TerminalSize {
    int columns = 0;
    int rows = 0;
};

// -----------------------------------------------------------------------
// Terminal — najniższa warstwa frameworka.
//
// Odpowiedzialność (i TYLKO to):
//   - wejście w "tryb GUI" terminala: raw mode + alternate screen buffer
//     + ukrycie systemowego kursora (na stałe — nasz GUI ma WŁASNY kursor
//     rysowany jako odwrócenie kolorów komórki, patrz etap 7),
//   - powrót do normalnego stanu terminala przy zamknięciu programu
//     (RAII) oraz przy sygnałach SIGINT/SIGTERM (żeby terminal nigdy nie
//     został "zepsuty" po crashu),
//   - surowy zapis/odczyt bajtów,
//   - odczyt rozmiaru terminala (w komórkach znakowych),
//   - wykrywanie zmiany rozmiaru (SIGWINCH).
//
// Terminal NIC nie wie o: komórkach, kolorach, klawiszach, widgetach.
// To celowe — wyższe warstwy (Screen, Renderer, KeyParser) budują się na
// tym, ale ta klasa pozostaje malutka i łatwa do przetestowania osobno.
//
// Użyty jest idiom pimpl: <termios.h>, <sys/ioctl.h> itd. nie wyciekają
// do publicznego nagłówka, więc reszta frameworka (i aplikacje) nie musi
// ich widzieć ani się przejmować platformą.
// -----------------------------------------------------------------------
class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;

    // Aktualny rozmiar terminala w komórkach znakowych.
    TerminalSize size() const;

    // Zapis surowych bajtów (np. już wyrenderowana klatka albo sekwencja ANSI).
    void write(std::string_view data);

    // Wymusza zrzut zbuforowanych danych do deskryptora terminala.
    void flush();

    // Blokujący/nieblokujący odczyt surowych bajtów wejścia.
    //   timeoutMs < 0  -> czekaj bez limitu,
    //   timeoutMs == 0 -> sprawdź natychmiast i wróć (non-blocking),
    //   timeoutMs > 0  -> czekaj maks. tyle milisekund.
    // Zwraca liczbę odczytanych bajtów (0 jeśli minął timeout).
    std::size_t read(char* buf, std::size_t maxLen, int timeoutMs = -1);

    // Zwraca true, jeśli od ostatniego wywołania terminal zmienił rozmiar
    // (SIGWINCH), i zeruje flagę. Warstwa wyżej (Screen) użyje tego, żeby
    // wiedzieć kiedy przebudować framebuffer.
    bool consumeResizeFlag();

private:
    void enableRawMode();
    void disableRawMode();
    void enterAlternateScreen();
    void leaveAlternateScreen();
    void hideSystemCursor();
    void showSystemCursor();
    void installSignalHandlers();
    void restoreSignalHandlers();

public:
    // Publiczne tylko dlatego, że handler sygnału (funkcja C, poza klasą)
    // musi się odwołać do tego typu przez wskaźnik. Aplikacje korzystające
    // z fenrir nie mają powodu, by same tego używać.
    struct Impl;

private:
    Impl* impl_;
};

} // namespace fenrir
