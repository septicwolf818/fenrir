#include "fenrir/core/Screen.hpp"

#include <algorithm>

namespace fenrir {

Screen::Screen(int width, int height)
    : width_(width), height_(height), cells_(static_cast<size_t>(width) * height) {}

void Screen::resize(int width, int height) {
    width_ = width;
    height_ = height;
    cells_.assign(static_cast<size_t>(width) * height, Cell{});
}

void Screen::clear(Cell fill) {
    std::fill(cells_.begin(), cells_.end(), fill);
}

void Screen::set(int x, int y, const Cell& cell) {
    if (!inBounds(x, y)) return;
    cells_[static_cast<size_t>(y) * width_ + x] = cell;
}

const Cell& Screen::at(int x, int y) const {
    static const Cell empty{};
    if (!inBounds(x, y)) return empty;
    return cells_[static_cast<size_t>(y) * width_ + x];
}

void Screen::putText(int x, int y, std::u32string_view text, Color fg, Color bg, uint8_t style) {
    int cx = x;
    for (char32_t ch : text) {
        if (cx >= width_) break;
        set(cx, y, Cell{ch, fg, bg, style});
        ++cx;
    }
}

} // namespace fenrir
