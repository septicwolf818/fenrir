#pragma once
#include <algorithm>
#include <functional>
#include <string>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Przycisk. Kolory z motywu (buttonBg/buttonText, na aktywnym buttonTextHot).
// Obsluguje Enter/Space wprost i przez wirtualny activate() — kontenery (Panel)
// aktywuja go bez wiedzy o typie.
class Button : public Widget {
public:
    explicit Button(std::u32string label) : label_(std::move(label)) {}

    std::function<void()> onActivate;

    void setText(std::u32string t) { label_ = std::move(t); }
    const std::u32string& text() const { return label_; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        const bool hot = focused_ || highlighted_;
        const Color fg = hot ? t.buttonTextHot : t.buttonText;
        const Color bg = t.buttonBg;

        screen.fillRect(bounds_, Cell{U' ', fg, bg, 0});

        // Etykieta przycieta do szerokosci — dlugie teksty nie wychodza poza prostokat.
        std::u32string label = text::clip(label_, std::max(0, bounds_.w - 4));
        std::u32string text = U"[ " + label + U" ]";
        if (static_cast<int>(text.size()) > bounds_.w) text = text::clip(text, bounds_.w);
        int cx = bounds_.x + std::max(0, (bounds_.w - static_cast<int>(text.size())) / 2);
        screen.putText(cx, bounds_.y + bounds_.h / 2, text, fg, bg, hot ? StyleBold : StyleNone);
    }

    bool activate() override {
        if (onActivate) onActivate();
        return true;
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.key == Key::Enter) return activate() ? KeyEventResult::Activated : KeyEventResult::Ignored;
        if (ev.key == Key::Char && !ev.ctrl && ev.character == U' ')
            return activate() ? KeyEventResult::Activated : KeyEventResult::Ignored;
        return KeyEventResult::Ignored;
    }

private:
    std::u32string label_;
};

} // namespace fenrir
