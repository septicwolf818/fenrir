#pragma once
#include <string_view>
#include <vector>

#include "fenrir/core/Types.hpp"

namespace fenrir {

// Screen is the "back buffer": we clear it every frame and everything (Windows,
// Widgets, Cursor) draws HERE, cell by cell. The Renderer then
// compares it against what it last sent to the terminal (its
// private "front buffer") and sends only the differences.
class Screen {
public:
    Screen(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    void resize(int width, int height);

    void clear(Cell fill = Cell{});
    void set(int x, int y, const Cell& cell);
    const Cell& at(int x, int y) const;

    // Fills a rectangle with the given cell (clipped to the screen).
    void fillRect(const Rect& r, const Cell& cell);

    // Convenient single-call text drawing; clipped to the screen bounds.
    void putText(int x, int y, std::u32string_view text, Color fg, Color bg, uint8_t style = 0);

    // ASCII frame with the given characters (corner / horizontal / vertical).
    void drawBorder(const Rect& r, char32_t corner, char32_t hEdge, char32_t vEdge, Color fg, Color bg);

    const std::vector<Cell>& cells() const { return cells_; }

private:
    int width_, height_;
    std::vector<Cell> cells_;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
};

} // namespace fenrir
