#pragma once
#include <string>

#include "fenrir/core/Wallpaper.hpp"

namespace fenrir::settings {

// Desktop settings, persistently stored in ~/.config/fenrir/settings.ini
// (simple key=value format). Desktop loads them at startup and applies them:
// theme (theme::apply), desktop name, taskbar clock, blinking cursor.
// The Settings application changes these values and saves them - the next
// start restores the user's choice.
struct Settings {
    std::string theme = "classic"; // id from theme::catalog()
    bool taskbarSeconds = false;   // taskbar clock shows seconds
    std::string desktopName = "Fenrir"; // name in the menu bar (right side)
    bool cursorBlink = true;       // blinking GUI cursor
    wallpaper::Wallpaper wallpaper; // desktop background (color or glyph pattern)
};

// Active settings (loaded once by Desktop at startup).
inline Settings active{};

// Path to the settings file: $HOME/.config/fenrir/settings.ini
std::string path();

// Loads settings from the file; no file = default values.
// Unknown keys/garbled lines are ignored; an unknown theme id
// falls back to "classic".
void load();

// Saves the current settings to the file (creating parent directories).
void save();

} // namespace fenrir::settings
