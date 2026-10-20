#include "fenrir/terminal/VtScreen.hpp"
#include "fenrir/terminal/PtyProcess.hpp"

#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <ctime>

using namespace fenrir;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

int main() {
    // Basic resize preserves content and cursor.
    {
        VtScreen v;
        v.resize(10, 4);
        const char* s = "ABCDEFGHIJKLMNO";
        v.feed(s, std::strlen(s));
        check(v.cursorX() == 5 && v.cursorY() == 1, "cursor before resize");
        v.resize(5, 6);
        check(v.cursorX() == 0 && v.cursorY() == 3, "cursor after width shrink");
        // content reflowed
        std::string row0;
        for (int x = 0; x < 5; ++x) row0 += static_cast<char>(v.at(x, 0).ch);
        check(row0 == "ABCDE", "reflow row0");
    }
    // An exact-width cursor is pending-wrap, not already on the next row.
    {
        VtScreen v;
        v.resize(5, 3);
        v.feed("ABCDE", 5);
        check(v.cursorX() == 5 && v.cursorY() == 0, "pending wrap before resize");
        v.resize(5, 4);
        check(v.cursorX() == 5 && v.cursorY() == 0, "pending wrap survives same-width resize");
        v.feed("F", 1);
        check(v.at(0, 1).ch == U'F', "next char wraps after resize");

        VtScreen saved;
        saved.resize(5, 4);
        saved.feed("ABCDE\x1b" "7", 7); // save the pending-wrap cursor
        saved.resize(5, 5);
        saved.feed("\x1b" "8", 2);
        check(saved.cursorX() == 5 && saved.cursorY() == 0, "saved pending-wrap cursor survives resize");

        VtScreen alternate;
        alternate.resize(5, 4);
        alternate.feed("ABCDE\x1b[?1049h", 13);
        alternate.resize(5, 5);
        alternate.feed("\x1b[?1049l", 8);
        check(alternate.cursorX() == 5 && alternate.cursorY() == 0,
              "1049 restores pending-wrap cursor after resize");
    }
    // A full row followed by CR/LF is a hard break, while automatic wrap is
    // soft; resizing must not join unrelated output lines.
    {
        VtScreen v;
        v.resize(5, 4);
        v.feed("ABCDE\r\nFG", 9);
        v.resize(4, 5);
        check(v.at(0, 0).ch == U'A' && v.at(3, 0).ch == U'D', "hard-break first row");
        check(v.at(0, 1).ch == U'E' && v.at(1, 1).ch == U' ', "hard-break continuation row");
        check(v.at(0, 2).ch == U'F' && v.at(1, 2).ch == U'G', "hard break preserved after reflow");

        VtScreen soft;
        soft.resize(5, 4);
        soft.feed("ABCDEF", 6);
        soft.resize(4, 5);
        check(soft.at(0, 1).ch == U'E' && soft.at(1, 1).ch == U'F', "soft wrap preserved after reflow");
    }
    // LF cancels pending autowrap rather than advancing an extra row.
    {
        VtScreen v;
        v.resize(5, 4);
        v.feed("ABCDE\nF", 7);
        check(v.at(4, 1).ch == U'F', "LF after exact-width line advances once");
        check(v.at(0, 2).ch == U' ', "LF does not trigger a second wrap");
    }
    // Exercise cursor offsets on both sides of width boundaries over repeated
    // width changes, including one-column windows and exact pending wraps.
    {
        for (int oldWidth = 2; oldWidth <= 8; ++oldWidth) {
            for (int count = 1; count <= 24; ++count) {
                std::string input;
                for (int i = 0; i < count; ++i)
                    input.push_back(static_cast<char>('A' + i % 26));
                for (int newWidth = 1; newWidth <= 8; ++newWidth) {
                    VtScreen v;
                    v.resize(oldWidth, 32);
                    v.feed(input.data(), input.size());
                    v.resize(newWidth, 32);
                    std::string actual;
                    for (int y = 0; y < v.height(); ++y)
                        for (int x = 0; x < v.width(); ++x)
                            if (v.at(x, y).ch != U' ')
                                actual.push_back(static_cast<char>(v.at(x, y).ch));
                    check(actual == input, "reflow retains text over width transitions");
                    if (count % oldWidth == 0 && count % newWidth == 0) {
                        check(v.cursorX() == newWidth && v.cursorY() == count / newWidth - 1,
                              "pending-wrap cursor maps to final occupied cell");
                    } else {
                        check(v.cursorX() == count % newWidth && v.cursorY() == count / newWidth,
                              "cursor maps to logical offset");
                    }
                }
            }
        }
    }
    // Height shrink keeps bottom content, not trailing blanks.
    {
        VtScreen v;
        v.resize(10, 4);
        v.feed("ABCDEFGHIJKLMNO", 15);
        v.resize(10, 6);
        v.resize(3, 3);
        std::string row0;
        for (int x = 0; x < 3; ++x) row0 += static_cast<char>(v.at(x, 0).ch);
        check(row0 == "GHI", "height shrink keeps content");
    }
    // Alternate screen round trip.
    {
        VtScreen v;
        v.resize(8, 4);
        v.feed("NORMAL", 6);
        v.feed("\x1b[?1049h", 8);
        check(v.inAltScreen(), "alt entered");
        v.feed("ALT", 3);
        check(v.at(0, 0).ch == U'A', "alt content");
        v.resize(4, 4);
        check(v.at(0, 0).ch == U'A', "alt survives resize");
        v.feed("\x1b[?1049l", 8);
        check(!v.inAltScreen(), "alt exited");
    }
    // Cursor anchors, including the normal cursor saved across 1049, reflow.
    {
        VtScreen v;
        v.resize(10, 6);
        v.feed("\x1b[4;8H", 6); // row 4, column 8
        check(v.cursorX() == 7 && v.cursorY() == 3, "position cursor on blank row");
        v.resize(5, 8);
        check(v.cursorX() == 2 && v.cursorY() == 4, "blank-row cursor reflow");

        v.resize(10, 6);
        v.feed("\x1b[3;9H", 6);
        v.feed("\x1b[?1049h", 8);
        v.feed("\x1b[4;8H", 6);
        v.resize(5, 8);
        check(v.cursorX() == 2 && v.cursorY() == 4, "alternate cursor reflow");
        v.feed("\x1b[?1049l", 8);
        check(v.cursorX() == 3 && v.cursorY() == 3, "restored normal cursor reflow");
    }
    // Scrollback view + resize.
    {
        VtScreen v;
        v.resize(5, 2);
        for (int i = 0; i < 6; ++i) { v.feed("line", 4); v.feed("\n", 1); }
        check(v.historySize() > 0, "history populated");
        v.scrollView(1);
        check(v.viewOffset() == 1, "scroll view");
        v.resize(4, 3);
        check(v.viewOffset() <= static_cast<int>(v.historyRowCount()), "view clamped");
    }
    // The scrollback keeps the actual text of a line, so it re-wraps to the
    // new width instead of being truncated or padded with blanks.
    {
        VtScreen v;
        v.resize(10, 2);
        v.feed("0123456789ABCDE\r\n", 17); // soft-wraps, then scrolls off
        v.feed("FGHIJKLMNO\r\n", 12);
        check(v.historySize() == 1, "wrapped line stored as one history line");
        v.resize(5, 4);
        check(v.historyRowCount() == 3, "history re-wrapped to the new width");
        v.scrollView(3);
        check(v.at(0, 0).ch == U'0' && v.at(4, 0).ch == U'4', "history row 0 re-wrapped");
        check(v.at(0, 1).ch == U'5' && v.at(4, 1).ch == U'9', "history row 1 re-wrapped");
        check(v.at(0, 2).ch == U'A' && v.at(4, 2).ch == U'E', "history row 2 re-wrapped");
        v.scrollView(-3);
        check(v.at(0, 0).ch == U'F', "live view restored");
    }
    // Separate output lines stay separate in the scrollback.
    {
        VtScreen v;
        v.resize(10, 2);
        v.feed("FIRST\r\n", 7);
        v.feed("SECOND\r\n", 8);
        v.feed("THIRD\r\n", 7);
        check(v.historySize() == 2, "two complete lines in history");
        v.resize(4, 4);
        check(v.historyRowCount() == 4, "history lines re-wrapped to the new width");
        v.scrollView(4);
        check(v.at(0, 0).ch == U'F' && v.at(3, 0).ch == U'S', "first line re-wrapped");
        check(v.at(0, 1).ch == U'T', "first line continues on the next row");
        check(v.at(0, 2).ch == U'S' && v.at(3, 2).ch == U'O', "second line re-wrapped");
        check(v.at(0, 3).ch == U'N' && v.at(1, 3).ch == U'D', "second line continues on the next row");
    }
    // Spaces written at the end of a soft-wrapped row are real cell offsets:
    // preserve them so the continuation does not shift left after reflow.
    {
        VtScreen v;
        v.resize(6, 2);
        v.feed("abc   X", 7);
        v.feed("\r\n", 2);
        v.feed("Y\r\n", 3);
        v.resize(4, 4);
        v.scrollView(2);
        check(v.at(0, 0).ch == U'a' && v.at(3, 0).ch == U' ', "soft-wrap trailing spaces retained");
        check(v.at(0, 1).ch == U' ' && v.at(1, 1).ch == U' ' && v.at(2, 1).ch == U'X',
              "soft-wrap continuation keeps its original offset");
    }
    // Clearing the screen breaks a soft-wrap continuation into the live grid;
    // subsequent output must start a new history line, not join the old one.
    {
        VtScreen v;
        v.resize(6, 2);
        v.feed("abcdefg\r\n", 9);
        check(v.historySize() == 1, "open soft-wrapped history line");
        v.feed("\x1b[2J\x1b[H", 7);
        v.feed("UVWXYZ12345\r\n", 13);
        check(v.historySize() == 2, "screen clear closes history continuation");
        v.resize(4, 4);
        check(v.historyRowCount() == 4, "cleared and new history lines remain distinct");
    }
    // Repeated resizing while a soft-wrapped line spans history and screen
    // must neither duplicate nor lose its cells.
    {
        const std::string input = "ABCDEFGHIJKLMNOPQRSTUVWX0123456789";
        VtScreen v;
        v.resize(6, 2);
        v.feed(input.data(), input.size());
        for (const auto [width, height] : {std::pair{4, 7}, {9, 6}, {3, 8}, {6, 7}}) {
            v.resize(width, height);
            const int historyRows = static_cast<int>(v.historyRowCount());
            const int totalRows = historyRows + v.height();
            std::vector<std::string> stream(static_cast<std::size_t>(totalRows),
                                            std::string(static_cast<std::size_t>(v.width()), ' '));
            for (int offset = historyRows; offset >= 0; --offset) {
                v.follow();
                v.scrollView(offset);
                for (int y = 0; y < v.height(); ++y) {
                    const int streamRow = historyRows - offset + y;
                    if (streamRow < 0 || streamRow >= totalRows) continue;
                    for (int x = 0; x < v.width(); ++x)
                        stream[static_cast<std::size_t>(streamRow)][static_cast<std::size_t>(x)] =
                            static_cast<char>(v.at(x, y).ch);
                }
            }
            std::string actual;
            for (const auto& row : stream)
                for (const char c : row)
                    if (c != ' ') actual.push_back(c);
            check(actual == input, "repeated resize preserves history/screen stream");
        }
    }
    // Multiple long hard lines must not be duplicated when resizes occur
    // between scrolls of their soft-wrapped rows.
    {
        VtScreen v;
        v.resize(8, 3);
        std::string expected;
        for (int i = 0; i < 12; ++i) {
            const std::string line = "L" + std::to_string(i) + "ABCDEFGHIJKLMNOPQRST";
            expected += line;
            v.feed(line.data(), line.size());
            v.feed("\r\n", 2);
            if (i == 3) v.resize(5, 5);
            if (i == 7) v.resize(11, 4);
        }
        const int historyRows = static_cast<int>(v.historyRowCount());
        const int totalRows = historyRows + v.height();
        std::vector<std::string> stream(static_cast<std::size_t>(totalRows),
                                        std::string(static_cast<std::size_t>(v.width()), ' '));
        for (int offset = historyRows; offset >= 0; --offset) {
            v.follow();
            v.scrollView(offset);
            for (int y = 0; y < v.height(); ++y) {
                const int streamRow = historyRows - offset + y;
                if (streamRow < 0 || streamRow >= totalRows) continue;
                for (int x = 0; x < v.width(); ++x)
                    stream[static_cast<std::size_t>(streamRow)][static_cast<std::size_t>(x)] =
                        static_cast<char>(v.at(x, y).ch);
            }
        }
        std::string actual;
        for (const auto& row : stream)
            for (const char c : row)
                if (c != ' ') actual.push_back(c);
        check(actual == expected, "hard lines and soft wraps do not duplicate after resize");
    }
    // Split VT sequence across feeds.
    {
        VtScreen v;
        v.resize(10, 4);
        const char* a = "AB\x1b[3";
        const char* b = "1mC";
        v.feed(a, std::strlen(a));
        v.feed(b, std::strlen(b));
        // Should not crash; C present somewhere.
        bool foundC = false;
        for (int y = 0; y < 4 && !foundC; ++y)
            for (int x = 0; x < 10; ++x)
                if (v.at(x, y).ch == U'C') foundC = true;
        check(foundC, "split sequence renders C");
    }
    // PTY basic smoke.
    {
        PtyProcess pty;
        std::string err;
        const char* previousShell = std::getenv("SHELL");
        const bool hadShell = previousShell != nullptr;
        const std::string savedShell = previousShell ? previousShell : "";
        setenv("SHELL", "/bin/sh", 1);
        const bool spawned = pty.spawn(20, 5, err);
        if (hadShell) setenv("SHELL", savedShell.c_str(), 1);
        else unsetenv("SHELL");
        check(spawned, "pty spawn");
        char buf[256];
        std::string out;
        auto waitFor = [&](const std::string& needle) {
            for (int i = 0; i < 100 && out.find(needle) == std::string::npos; ++i) {
                const long n = pty.readSome(buf, sizeof(buf));
                if (n > 0) out.append(buf, static_cast<std::size_t>(n));
                else if (n < 0) break;
                const struct timespec ts{0, 10000000};
                nanosleep(&ts, nullptr);
            }
            return out.find(needle) != std::string::npos;
        };
        pty.writeBytes("printf 'PTY_OK\\n'\n");
        check(waitFor("PTY_OK\r\n"), "pty child output");
        pty.writeBytes("stty size\n");
        check(waitFor("5 20\r\n"), "initial child PTY size");
        pty.resize(10, 3);
        pty.writeBytes("stty size\n");
        check(waitFor("3 10\r\n"), "resized child PTY size");
        pty.terminate();
    }
    std::cout << "All tests passed.\n";
    return 0;
}
