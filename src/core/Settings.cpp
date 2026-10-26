#include "fenrir/core/Settings.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string_view>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
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

// ANSI palette index 0..15; garbled values keep the current setting.
uint8_t parseIndex(std::string_view v, uint8_t fallback) {
    if (v.empty()) return fallback;
    unsigned long n = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return fallback;
        n = n * 10 + static_cast<unsigned long>(c - '0');
        if (n > 15) return fallback;
    }
    return static_cast<uint8_t>(n);
}

} // namespace

std::string path() {
    return fs::join(fs::join(fs::join(fs::homeDir(), ".config"), "fenrir"), "settings.ini");
}

void load() {
    dirty = false;
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
        } else if (key == "cursor_acceleration") {
            active.cursorAcceleration = parseBool(val, active.cursorAcceleration);
        } else if (key == "cursor_max_speed") {
            try { active.cursorMaxSpeed = std::stoi(val); } catch (...) {}
            if (active.cursorMaxSpeed < 1) active.cursorMaxSpeed = 1;
            if (active.cursorMaxSpeed > 10) active.cursorMaxSpeed = 10;
        } else if (key == "app_notifications") {
            active.appNotifications = parseBool(val, active.appNotifications);
        } else if (key == "wallpaper_enabled") {
            active.wallpaper.enabled = parseBool(val, active.wallpaper.enabled);
        } else if (key == "wallpaper_mode") {
            active.wallpaper.mode = (val == "pattern") ? wallpaper::Wallpaper::Mode::Pattern
                                                      : wallpaper::Wallpaper::Mode::Solid;
        } else if (key == "wallpaper_bg") {
            active.wallpaper.bg = parseIndex(val, active.wallpaper.bg);
        } else if (key == "wallpaper_fg") {
            active.wallpaper.fg = parseIndex(val, active.wallpaper.fg);
        } else if (key == "wallpaper_glyph") {
            const std::u32string u = text::toUtf32(val);
            if (!u.empty()) active.wallpaper.glyph = u.front();
        }
        // Unknown keys are ignored — forward compatibility.
    }
}

void save() {
    if (!dirty) return;
    dirty = false;
    std::error_code ec;
    std::filesystem::path p(path());
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::trunc);
    if (!f) return; // a failed save is not fatal — settings keep working for the session

    f << "# Fenrir settings (key=value; changes take effect after saving)\n"
      << "theme=" << active.theme << "\n"
      << "taskbar_seconds=" << (active.taskbarSeconds ? "1" : "0") << "\n"
      << "desktop_name=" << active.desktopName << "\n"
      << "cursor_blink=" << (active.cursorBlink ? "1" : "0") << "\n"
      << "cursor_acceleration=" << (active.cursorAcceleration ? "1" : "0") << "\n"
      << "cursor_max_speed=" << active.cursorMaxSpeed << "\n"
      << "app_notifications=" << (active.appNotifications ? "1" : "0") << "\n"
      << "wallpaper_enabled=" << (active.wallpaper.enabled ? "1" : "0") << "\n"
      << "wallpaper_mode=" << (active.wallpaper.mode == wallpaper::Wallpaper::Mode::Pattern ? "pattern" : "solid") << "\n"
      << "wallpaper_bg=" << static_cast<int>(active.wallpaper.bg) << "\n"
      << "wallpaper_fg=" << static_cast<int>(active.wallpaper.fg) << "\n"
      << "wallpaper_glyph=" << text::toUtf8(std::u32string(1, active.wallpaper.glyph)) << "\n";
}

void markDirty() { dirty = true; }

bool isDirty() { return dirty; }

void saveIfDirty() { if (dirty) save(); }

} // namespace fenrir::settings
