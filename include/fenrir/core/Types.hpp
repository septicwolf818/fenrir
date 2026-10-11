#pragma once
#include <cstdint>

namespace fenrir {

// 16 kolorów ANSI — wartości RGB odpowiadają dokładnie tym, co zwraca nearest() w Renderer.
// Win95-inspired palette: granatowe tytuły, szare tła, biały tekst, czarne na niebieskim.
struct Color {
    uint8_t r = 0, g = 0, b = 0;
    constexpr bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
    constexpr bool operator!=(const Color& o) const { return !(*this == o); }
};

namespace colors {
// ANSI 0-7 (normal)
inline constexpr Color Black{0, 0, 0};
inline constexpr Color Red{128, 0, 0};
inline constexpr Color Green{0, 128, 0};
inline constexpr Color Yellow{128, 128, 0};
inline constexpr Color Blue{0, 0, 128};
inline constexpr Color Magenta{128, 0, 128};
inline constexpr Color Cyan{0, 128, 128};
inline constexpr Color White{192, 192, 192};
// ANSI 8-15 (bright)
inline constexpr Color Gray{128, 128, 128};
inline constexpr Color BrightRed{255, 0, 0};
inline constexpr Color BrightGreen{0, 255, 0};
inline constexpr Color BrightYellow{255, 255, 0};
inline constexpr Color BrightBlue{0, 0, 255};
inline constexpr Color BrightMagenta{255, 0, 255};
inline constexpr Color BrightCyan{0, 255, 255};
inline constexpr Color BrightWhite{255, 255, 255};
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
