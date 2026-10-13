#include "fenrir/core/Settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Theme.hpp"

namespace fenrir::settings {

namespace {

std::string trim(std::string_view s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

bool parseBool(std::string_view v, bool fallback) {
    if (v == "1" || v == "true" || v == "on" || v == "yes") return true;
    if (v == "0" || v == "false" || v == "off" || v == "no") return false;
    return fallback;
}

} // namespace

std::string path() {
    return fs::join(fs::join(fs::join(fs::homeDir(), ".config"), "fenrir"), "settings.ini");
}

void load() {
    // Default values already live in `active` (struct Settings).
    // An unknown theme id falls back to classic — apply() ignores unknown ids,
    // so we set the key manually in case the catalog changed.
    const std::string file = path(); // local copy — the stream can't hold the temporary
    std::ifstream f(file);
    if (!f) return; // no file = default settings

    std::string line;
    while (std::getline(f, line)) {
        std::string sv = trim(line); // std::string, not string_view — trim returns a temporary
        if (sv.empty() || sv.front() == '#' || sv.front() == ';') continue;
        std::size_t eq = sv.find('=');
        if (eq == std::string_view::npos) continue;
        std::string key = trim(sv.substr(0, eq));
        std::string val = trim(sv.substr(eq + 1));

        if (key == "theme") {
            active.theme = theme::find(val) ? val : "classic";
        } else if (key == "taskbar_seconds") {
            active.taskbarSeconds = parseBool(val, active.taskbarSeconds);
        } else if (key == "desktop_name") {
            active.desktopName = val; // empty -> the taskbar shows the startup name (fallback in Desktop)
        } else if (key == "cursor_blink") {
            active.cursorBlink = parseBool(val, active.cursorBlink);
        }
        // Unknown keys are ignored — forward compatibility.
    }
}

void save() {
    std::error_code ec;
    std::filesystem::path p(path());
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::trunc);
    if (!f) return; // a failed save is not fatal — settings keep working for the session

    f << "# Fenrir settings (key=value; changes take effect after saving)\n"
      << "theme=" << active.theme << "\n"
      << "taskbar_seconds=" << (active.taskbarSeconds ? "1" : "0") << "\n"
      << "desktop_name=" << active.desktopName << "\n"
      << "cursor_blink=" << (active.cursorBlink ? "1" : "0") << "\n";
}

} // namespace fenrir::settings
