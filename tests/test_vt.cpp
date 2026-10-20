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
    // Scrollback view + resize.
    {
        VtScreen v;
        v.resize(5, 2);
        for (int i = 0; i < 6; ++i) { v.feed("line", 4); v.feed("\n", 1); }
        check(v.historySize() > 0, "history populated");
        v.scrollView(1);
        check(v.viewOffset() == 1, "scroll view");
        v.resize(4, 3);
        check(v.viewOffset() <= static_cast<int>(v.historySize()), "view clamped");
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
        check(pty.spawn(20, 5, err), "pty spawn");
        pty.writeBytes("echo PTY_OK\n");
        // pump reads
        char buf[256];
        std::string out;
        for (int i = 0; i < 50 && out.find("PTY_OK") == std::string::npos; ++i) {
            long n = pty.readSome(buf, sizeof(buf));
            if (n > 0) out.append(buf, static_cast<std::size_t>(n));
            else if (n == 0) { /* EAGAIN */ }
            else break;
            // allow child to run
            struct timespec ts{0, 10000000};
            nanosleep(&ts, nullptr);
        }
        check(out.find("PTY_OK") != std::string::npos, "pty output");
        pty.resize(10, 3);
        pty.terminate();
    }
    std::cout << "All tests passed.\n";
    return 0;
}
