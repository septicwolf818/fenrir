#pragma once
#include <functional>
#include <string>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

class Button : public Widget {
public:
    explicit Button(std::u32string label) : label_(std::move(label)) {}

    std::function<void()> onActivate;

    void draw(Screen& screen) override {
        // Przycisk Win95: Gray tlo zawsze. Nieaktywny = Black tekst (ANSI 30);
        // aktywny (fokus/highlight) = White tekst (bold). Bez białego tła.
        Color fg = (focused_ || highlighted_) ? colors::White : colors::Black;
        Color bg = colors::Gray;
        uint8_t style = (focused_ || highlighted_) ? StyleBold : StyleNone;
        for (int y = bounds_.y; y < bounds_.y + bounds_.h; ++y)
            for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x)
                screen.set(x, y, Cell{U' ', fg, bg, 0});
        std::u32string text = U"[ " + label_ + U" ]";
        int cx = bounds_.x + std::max(0, (bounds_.w - static_cast<int>(text.size())) / 2);
        screen.putText(cx, bounds_.y + bounds_.h / 2, text, fg, bg, StyleBold | style);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.key == Key::Enter) {
            if (onActivate) onActivate();
            return KeyEventResult::Activated;
        }
        if (ev.key == Key::Char && !ev.ctrl && ev.character == U' ') {
            if (onActivate) onActivate();
            return KeyEventResult::Activated;
        }
        return KeyEventResult::Ignored;
    }

private:
    std::u32string label_;
};

} // namespace fenrir
