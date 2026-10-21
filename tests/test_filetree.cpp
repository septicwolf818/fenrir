#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/FileTree.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

using namespace fenrir;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << "\n";
        std::exit(1);
    }
}

static std::string screenText(const Screen& screen) {
    std::string out;
    for (int y = 0; y < screen.height(); ++y)
        for (int x = 0; x < screen.width(); ++x)
            out += static_cast<char>(screen.at(x, y).character);
    return out;
}

int main() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "fenrir_filetree_watchdog";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sub");
    std::filesystem::create_directories(root / "sub" / "deep");

    FileTree tree;
    tree.setRoot(root.string());
    tree.setBounds(Rect{0, 0, 40, 12});
    Screen screen(40, 12);

    tree.draw(screen);
    check(screenText(screen).find("sub") != std::string::npos, "root lists the subdirectory");

    // Expand the "sub" row (row 0 is the root, row 1 is "sub").
    tree.handleKey(KeyEvent{Key::Down});
    tree.handleKey(KeyEvent{Key::Enter});
    tree.draw(screen);
    check(screenText(screen).find("deep") != std::string::npos, "expanded subdirectory lists its child");

    // The watchdog polls at most every kPollIntervalMs — wait for the next poll.
    const auto waitForChange = [&tree] {
        for (int i = 0; i < 60; ++i) {
            if (tree.pollFilesystem()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    };

    // A watched directory that disappears must be dropped from the tree.
    std::filesystem::remove_all(root / "sub");
    check(waitForChange(), "watchdog reports the removed directory");
    tree.draw(screen);
    check(screenText(screen).find("sub") == std::string::npos, "removed directory disappears");
    check(screenText(screen).find("deep") == std::string::npos, "removed subtree disappears");

    // A new entry in a watched directory must show up.
    {
        std::ofstream file(root / "new.txt");
        file << "x";
    }
    check(waitForChange(), "watchdog reports the new file");
    tree.draw(screen);
    check(screenText(screen).find("new.txt") != std::string::npos, "new file appears");

    // The watched root itself disappearing must be noticed as well: the root
    // row then shows the collapsed marker (the tree stopped watching it).
    std::filesystem::remove_all(root);
    check(waitForChange(), "watchdog reports the removed root");
    tree.draw(screen);
    check(screenText(screen).find("[-]") == std::string::npos, "removed root is not watched anymore");

    std::cout << "All FileTree tests passed.\n";
    return 0;
}
