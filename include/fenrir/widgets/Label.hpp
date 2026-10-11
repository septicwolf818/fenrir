#pragma once
#include <algorithm>
#include <string>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Wyrownanie tekstu wewnatreznej szerokości Labela.
enum class LabelAlign { Left, Center, Right };

// Tekst (statyczny lub zmieniany w locie przez setText()). Tło wypełnia
// cały prostokąt Labela; tekst jest wyrównany wg align_ (domyślnie Left).
// Kolory z konstruktora są zachowane przy każdym setText().
class Label : public Widget {
public:
    explicit Label(std::u32string text, Color fg = colors::White, Color bg = colors::Black)
        : text_(std::move(text)), fg_(fg), bg_(bg) {}

    void setText(std::u32string t) { text_ = std::move(t); }
    const std::u32string& text() const { return text_; }
    void setAlignment(LabelAlign a) { align_ = a; }

    void draw(Screen& screen) override {
        for (int y = bounds_.y; y < bounds_.y + bounds_.h; ++y)
            for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x)
                screen.set(x, y, Cell{U' ', fg_, bg_, 0});
        int off = 0;
        int tw = static_cast<int>(text_.size());
        if (tw < bounds_.w) {
            if (align_ == LabelAlign::Center) off = (bounds_.w - tw) / 2;
            else if (align_ == LabelAlign::Right) off = bounds_.w - tw;
        }
        screen.putText(bounds_.x + std::max(0, off), bounds_.y, text_, fg_, bg_);
    }

    KeyEventResult handleKey(const KeyEvent&) override { return KeyEventResult::Ignored; }

private:
    std::u32string text_;
    Color fg_, bg_;
    LabelAlign align_ = LabelAlign::Left;
};

} // namespace fenrir
