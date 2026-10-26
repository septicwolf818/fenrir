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
    bool cursorAcceleration = true; // accelerate the virtual cursor while an arrow is held
    int cursorMaxSpeed = 5;        // maximum cursor speed multiplier (1-10)
    bool appNotifications = true;  // allow apps to push notifications
    wallpaper::Wallpaper wallpaper; // desktop background (color or glyph pattern)
};

// Active settings (loaded once by Desktop at startup).
inline Settings active{};
// True when in-memory settings differ from the last saved file.
inline bool dirty = false;

// Path to the settings file: $HOME/.config/fenrir/settings.ini
std::string path();

// Loads settings from the file; no file = default values.
// Unknown keys/garbled lines are ignored; an unknown theme id
// falls back to "classic".
void load();

// Saves the current settings to the file (creating parent directories).
// save() is a no-op when nothing has changed since the last save.
void save();
void markDirty();
bool isDirty();
void saveIfDirty();

} // namespace fenrir::settings
