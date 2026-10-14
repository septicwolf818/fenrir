#pragma once
#include <cstddef>
#include <cstdint>

namespace fenrir::wallpaper {

// Desktop wallpaper: either a solid color or a repeating glyph pattern
// (foreground character on a background color). Desktop::drawDesktopBackground
// reads settings::active.wallpaper on every tick, so changes in the Settings
// app apply instantly; settings::save/load persist it to settings.ini.
struct Wallpaper {
    bool enabled = true;
    enum class Mode : uint8_t { Solid, Pattern };
    Mode mode = Mode::Solid;
    uint8_t bg = 0;         // ANSI palette index 0..15 (both modes)
    uint8_t fg = 7;         // ANSI palette index 0..15 (pattern mode only)
    char32_t glyph = U'░';  // repeated cell (pattern mode only)
};

// Human-readable names for the 16 ANSI palette entries (0..15).
inline const char* colorName(uint8_t index) {
    static constexpr const char* names[16] = {
        "Black",  "Red",     "Green",  "Yellow",  "Blue",    "Magenta", "Cyan",  "Light gray",
        "Dark gray", "Bright red", "Bright green", "Bright yellow",
        "Bright blue", "Bright magenta", "Bright cyan", "White"};
    return names[index & 15];
}

// Glyphs the Settings app cycles through for pattern mode.
inline constexpr char32_t glyphPresets[] = {
    U'.', U':', U'·', U'*', U'+', U'x', U'#', U'%', U'=', U'░', U'▒', U'▓'};
inline constexpr std::size_t glyphPresetCount = sizeof(glyphPresets) / sizeof(glyphPresets[0]);

} // namespace fenrir::wallpaper
