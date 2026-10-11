#pragma once
#include <string_view>
#include <vector>

#include "fenrir/core/Types.hpp"

namespace fenrir {

// Screen to "back buffer": co klatkę czyścimy go i wszystko (Windowy,
// Widgety, Cursor) rysuje się TU, komórka po komórce. Renderer dopiero
// potem porównuje to z tym, co ostatnio wysłał do terminala (jego
// prywatny "front buffer") i wysyła tylko różnice.
class Screen {
public:
    Screen(int width, int height);

    int width() const { return width_; }
    int height() const { return height_; }
    void resize(int width, int height);

    void clear(Cell fill = Cell{});
    void set(int x, int y, const Cell& cell);
    const Cell& at(int x, int y) const;

    // Wypelnia prostokat podana komorka (przycity do ekranu).
    void fillRect(const Rect& r, const Cell& cell);

    // Wygodne rysowanie tekstu jednym wywołaniem; przycina do granic ekranu.
    void putText(int x, int y, std::u32string_view text, Color fg, Color bg, uint8_t style = 0);

    // Ramka ASCII o podanych znakach (rog / poziom / pion).
    void drawBorder(const Rect& r, char32_t corner, char32_t hEdge, char32_t vEdge, Color fg, Color bg);

    const std::vector<Cell>& cells() const { return cells_; }

private:
    int width_, height_;
    std::vector<Cell> cells_;

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
};

} // namespace fenrir
