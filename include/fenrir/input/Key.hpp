#pragma once
#include <cstdint>

namespace fenrir {

enum class Key {
    Unknown,
    Char,       // plain character (incl. Unicode) -> see KeyEvent::character
    Up, Down, Left, Right,
    Enter, Escape, Backspace, Tab, Delete,
    Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct KeyEvent {
    Key key = Key::Unknown;
    char32_t character = 0; // valid when key == Key::Char (or ctrl==true)
    bool ctrl = false;      // e.g. ctrl=true, character=U'S' -> Ctrl+S
    // Alt (meta). The Linux console has no separate byte for Alt+X — by default
    // (VC_META mode) it sends ESC + X, so KeyParser assembles an event
    // with alt=true from that. Practical combos on a bare TTY: Alt+Tab (ESC+Tab) and
    // Alt+composite key (ESC+ESC[5~).
    bool alt = false;
    // Shift. Arrows with a modifier arrive as ESC [ 1 ; <mods> A (the xterm
    // form); the Linux console sends the same sequence for Shift+Arrow, so
    // Shift+Arrow (text selection) works on a bare console too.
    bool shift = false;
};


} // namespace fenrir
