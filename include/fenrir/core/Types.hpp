#pragma once
#include <cstdint>

namespace fenrir {

// Color = index 0..15 in the ANSI palette. Linux TTY: 16 colors only,
// no truecolor (38;2;R;G;B) — the renderer sends the SGR code directly.
// Functions mapping names to numbers (30-37/90-97 etc.) live in the Renderer.
struct Color {
    uint8_t index = 0;
    constexpr bool operator==(const Color& o) const { return index == o.index; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

namespace colors {
// ANSI 0-7 (normal)
inline constexpr Color Black{0};
inline constexpr Color Red{1};
inline constexpr Color Green{2};
inline constexpr Color Yellow{3};
inline constexpr Color Blue{4};
inline constexpr Color Magenta{5};
inline constexpr Color Cyan{6};
inline constexpr Color White{7}; // "off-white" (light gray)
// ANSI 8-15 (bright)
inline constexpr Color Gray{8};
inline constexpr Color BrightRed{9};
inline constexpr Color BrightGreen{10};
inline constexpr Color BrightYellow{11};
inline constexpr Color BrightBlue{12};
inline constexpr Color BrightMagenta{13};
inline constexpr Color BrightCyan{14};
inline constexpr Color BrightWhite{15};
} // namespace colors

enum StyleFlag : uint8_t {
    StyleNone = 0,
    StyleBold = 1 << 0,
    StyleUnderline = 1 << 1,
    StyleDim = 1 << 2,
};

struct Cell {
    char32_t character = U' ';
    Color fg = colors::White;
    Color bg = colors::Black;
    uint8_t style = StyleNone;

    bool operator==(const Cell& o) const {
        return character == o.character && fg == o.fg && bg == o.bg && style == o.style;
    }
    bool operator!=(const Cell& o) const { return !(*this == o); }
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

} // namespace fenrir
