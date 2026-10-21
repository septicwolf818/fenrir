// Test runner: ONE binary with all the suites (the sources are split by
// area, the target is one — see CMakeLists.txt).
//
// Each suite lives in its own file and exposes a run*Tests() function; a
// failing check throws (test_harness.hpp) and the runner reports the suite
// as failed and carries on with the next one.
#include "test_harness.hpp"

#include <exception>
#include <iostream>
#include <vector>

// The suites — one per area.
void runTerminalTests();
void runFileTreeTests();
void runWatchdogTests();
void runEditorTests();
void runClipboardTests();

namespace {

struct Suite {
    const char* name;
    void (*run)();
};

int runSuite(const Suite& suite) {
    try {
        suite.run();
    } catch (const std::exception& e) {
        std::cerr << "FAIL [" << suite.name << "] " << e.what() << "\n";
        return 1;
    }
    std::cout << "ok   [" << suite.name << "]\n";
    return 0;
}

} // namespace

int main() {
    const std::vector<Suite> suites{
        {"terminal buffer", runTerminalTests},
        {"file tree", runFileTreeTests},
        {"filesystem watchdog", runWatchdogTests},
        {"text editor", runEditorTests},
        {"clipboard", runClipboardTests},
    };

    int failed = 0;
    for (const auto& suite : suites) failed += runSuite(suite);

    if (failed > 0) {
        std::cerr << failed << " of " << suites.size() << " test suites failed.\n";
        return 1;
    }
    std::cout << "All " << suites.size() << " test suites passed.\n";
    return 0;
}
