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

    Cell c = screen.at(x_, y_);
    std::swap(c.fg, c.bg);
    // Set StyleBold — without it the renderer compares the cell with previous_ and sees "no change".
    c.style |= StyleBold;
    screen.set(x_, y_, c);
}

} // namespace fenrir
