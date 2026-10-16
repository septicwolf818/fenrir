#include "fenrir/cursor/Cursor.hpp"

#include <algorithm>

namespace fenrir {

void Cursor::clamp(int width, int height) {
    x_ = std::clamp(x_, 0, std::max(0, width - 1));
    y_ = std::clamp(y_, 0, std::max(0, height - 1));
}

void Cursor::render(Screen& screen) const {
    if (!visible_) return;
    if (x_ < 0 || y_ < 0 || x_ >= screen.width() || y_ >= screen.height()) return;

    // Solid high-contrast block (hardware-cursor style). An fg/bg swap of the
    // underlying cell is NOT enough: it vanishes on same-color cells (e.g. a
    // pattern-wallpaper cell whose glyph color equals its background) and on
    // low-contrast pairs (black on dark gray, where even bold lands on the
    // background color). A fixed black-on-white block stays visible anywhere.
    // Set StyleBold — without it the renderer compares the cell with previous_ and sees "no change".
    screen.set(x_, y_, Cell{U'█', colors::Black, colors::White, StyleBold});
}

} // namespace fenrir
