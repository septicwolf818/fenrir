#pragma once
#include <cstdint>

namespace fenrir {

enum class Key {
    Unknown,
    Char,       // zwykły znak (w tym Unicode) -> patrz KeyEvent::character
    Up, Down, Left, Right,
    Enter, Escape, Backspace, Tab, Delete,
    Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct KeyEvent {
    Key key = Key::Unknown;
    char32_t character = 0; // ważne gdy key == Key::Char (albo ctrl==true)
    bool ctrl = false;      // np. ctrl=true, character=U'S' -> Ctrl+S
};

} // namespace fenrir
