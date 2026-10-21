#include "fenrir/core/FileWatchdog.hpp"
#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/DirectoryView.hpp"
#include "fenrir/widgets/TextArea.hpp"

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

static void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << content;
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
        std::filesystem::temp_directory_path() / "fenrir_watchdog_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // --- A watched file: modified, removed, recreated ----------------------
    {
        const std::filesystem::path file = root / "a.txt";
        writeFile(file, "one");
        FileWatchdog watchdog;
        watchdog.watch(file.string());
        check(watchdog.pollNow().empty(), "first poll only records the state");

        writeFile(file, "two - longer content");
        const auto modified = watchdog.pollNow();
        check(modified.size() == 1 && modified[0].change == FileWatchdog::Change::Modified,
              "modified file is reported");

        std::filesystem::remove(file);
        const auto removed = watchdog.pollNow();
        check(removed.size() == 1 && removed[0].change == FileWatchdog::Change::Removed,
              "removed file is reported");

        writeFile(file, "back");
        const auto created = watchdog.pollNow();
        check(created.size() == 1 && created[0].change == FileWatchdog::Change::Created,
              "recreated file is reported");

        watchdog.unwatch(file.string());
        check(!watchdog.watches(file.string()), "unwatch removes the path");
    }

    // --- A watched directory: entries added, directory removed --------------
    {
        const std::filesystem::path dir = root / "dir";
        std::filesystem::create_directories(dir);
        FileWatchdog watchdog;
        watchdog.watch(dir.string());
        check(watchdog.pollNow().empty(), "first directory poll only records the state");

        writeFile(dir / "new.txt", "x");
        const auto changed = watchdog.pollNow();
        check(changed.size() == 1 && changed[0].change == FileWatchdog::Change::Modified,
              "new entry in a directory is reported");

        std::filesystem::remove_all(dir);
        const auto removed = watchdog.pollNow();
        check(removed.size() == 1 && removed[0].change == FileWatchdog::Change::Removed,
              "removed directory is reported");
    }

    // --- The file manager panel refreshes when its directory changes --------
    {
        const std::filesystem::path dir = root / "panel";
        std::filesystem::create_directories(dir);
        DirectoryView view(dir.string());
        view.setBounds(Rect{0, 0, 30, 8});
        Screen screen(30, 8);
        view.draw(screen);
        check(screenText(screen).find("first.txt") == std::string::npos, "panel starts empty");

        writeFile(dir / "first.txt", "x");
        // The watchdog polls at most every kPollIntervalMs.
        std::this_thread::sleep_for(std::chrono::milliseconds(FileWatchdog::kPollIntervalMs + 150));
        view.draw(screen);
        check(screenText(screen).find("first.txt") != std::string::npos,
              "panel shows a file created outside the app");
    }

    // --- The editor notices a file that changed or disappeared --------------
    {
        const std::filesystem::path file = root / "edit.txt";
        writeFile(file, "original");
        TextArea area;
        area.loadFile(file.string());
        check(area.fileStamp().exists, "loaded file is on disk");
        check(!FileWatchdog::differsFrom(area.fileStamp(), FileWatchdog::stateOf(file.string())),
              "a freshly loaded file is unchanged");

        writeFile(file, "changed by someone else");
        check(FileWatchdog::differsFrom(area.fileStamp(), FileWatchdog::stateOf(file.string())),
              "external change is detected");

        std::filesystem::remove(file);
        const auto now = FileWatchdog::stateOf(file.string());
        check(area.fileStamp().exists && !now.exists, "a removed file is detected as missing");

        // Saving updates the stamp — the file is no longer "changed".
        writeFile(file, "changed by someone else");
        area.saveFile(file.string());
        check(!FileWatchdog::differsFrom(area.fileStamp(), FileWatchdog::stateOf(file.string())),
              "saving updates the remembered state");
    }

    std::filesystem::remove_all(root);
    std::cout << "All watchdog tests passed.\n";
    return 0;
}
