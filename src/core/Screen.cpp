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

void Screen::fillRect(const Rect& r, const Cell& cell) {
    const int x0 = std::max(0, r.x);
    const int y0 = std::max(0, r.y);
    const int x1 = std::min(width_, r.x + r.w);
    const int y1 = std::min(height_, r.y + r.h);
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            cells_[static_cast<size_t>(y) * width_ + x] = cell;
}

void Screen::putText(int x, int y, std::u32string_view text, Color fg, Color bg, uint8_t style) {
    int cx = x;
    for (char32_t ch : text) {
        if (cx >= width_) break;
        // Control characters must never reach a cell: the renderer emits cell
        // content as raw bytes, and e.g. '\n' would move the terminal cursor
        // and corrupt the positioning of everything drawn after it. Render
        // them as spaces (1:1 width, layout unchanged); the renderer has its
        // own backstop that shows them as visible glyphs if any slip through.
        if (ch == U'\n' || ch == U'\r' || ch == U'\t') ch = U' ';
        set(cx, y, Cell{ch, fg, bg, style});
        ++cx;
    }
}

void Screen::drawBorder(const Rect& r, char32_t corner, char32_t hEdge, char32_t vEdge, Color fg, Color bg) {
    if (r.w < 2 || r.h < 2) return;
    const int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    set(x0, y0, Cell{corner, fg, bg, 0});
    set(x1, y0, Cell{corner, fg, bg, 0});
    set(x0, y1, Cell{corner, fg, bg, 0});
    set(x1, y1, Cell{corner, fg, bg, 0});
    for (int x = x0 + 1; x < x1; ++x) {
        set(x, y0, Cell{hEdge, fg, bg, 0});
        set(x, y1, Cell{hEdge, fg, bg, 0});
    }
    for (int y = y0 + 1; y < y1; ++y) {
        set(x0, y, Cell{vEdge, fg, bg, 0});
        set(x1, y, Cell{vEdge, fg, bg, 0});
    }
}

} // namespace fenrir
