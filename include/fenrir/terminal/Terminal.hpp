#pragma once

#include <cstddef>
#include <string_view>

namespace fenrir {

struct TerminalSize {
    int columns = 0;
    int rows = 0;
};

// -----------------------------------------------------------------------
// Terminal — the lowest layer of the framework.
//
// Responsibility (and ONLY this):
//   - putting the terminal into "GUI mode": raw mode + alternate screen buffer
//     + hiding the system cursor (permanently — our GUI has its OWN cursor,
//     a solid block stamped over the cell, see stage 7),
//   - restoring the normal terminal state when the program exits
//     (RAII) and on SIGINT/SIGTERM (so the terminal is never
//     left "broken" after a crash),
//   - raw byte write/read,
//   - reading the terminal size (in character cells),
//   - detecting size changes (SIGWINCH).
//
// Terminal knows NOTHING about: cells, colors, keys, widgets.
// That is deliberate — the higher layers (Screen, Renderer, KeyParser) build on
// this, but this class stays tiny and easy to test in isolation.
//
// The pimpl idiom is used: <termios.h>, <sys/ioctl.h>, etc. do not leak
// into the public header, so the rest of the framework (and applications) never
// needs to see them or worry about the platform.
// -----------------------------------------------------------------------
class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;

    // Current terminal size in character cells.
    TerminalSize size() const;

    // Writes raw bytes (e.g. an already-rendered frame or an ANSI sequence).
    void write(std::string_view data);

    // Forces buffered data out to the terminal file descriptor.
    void flush();

    // Blocking/non-blocking raw input byte read.
    //   timeoutMs < 0  -> wait indefinitely,
    //   timeoutMs == 0 -> check immediately and return (non-blocking),
    //   timeoutMs > 0  -> wait at most that many milliseconds.
    // Returns the number of bytes read (0 if the timeout elapsed).
    std::size_t read(char* buf, std::size_t maxLen, int timeoutMs = -1);

    // Returns true if the terminal has changed size since the last call
    // (SIGWINCH), and clears the flag. The layer above (Screen) uses this to
    // know when to rebuild the framebuffer.
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
    // Public only because the signal handler (a C function, outside the class)
    // must refer to this type through a pointer. Applications built on
    // fenrir have no reason to use this themselves.
    struct Impl;

private:
    Impl* impl_;
};

} // namespace fenrir
