#pragma once
#include <algorithm>
#include <functional>
#include <string>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Button. Colors from the theme (buttonBg/buttonText, buttonTextHot when active).
// Handles Enter/Space directly and through the virtual activate() — containers (Panel)
// activate it without knowing its type.
class Button : public Widget {
public:
    explicit Button(std::u32string label) : label_(std::move(label)) {}

    std::function<void()> onActivate;

    void setText(std::u32string t) { label_ = std::move(t); }
    const std::u32string& text() const { return label_; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        const bool hot = (focused_ || highlighted_) && enabled_;
        // The theme's dimText color controls disabled contrast; do not also
        // apply ANSI dim, which can turn a bright theme color into gray.
        const Color fg = enabled_ ? (hot ? t.buttonTextHot : t.buttonText) : t.dimText;
        const Color bg = t.buttonBg;
        const uint8_t style = enabled_ && hot ? StyleBold : StyleNone;

        screen.fillRect(bounds_, Cell{U' ', fg, bg, 0});

        // Label clipped to the width — long text does not spill past the rectangle.
        std::u32string label = text::clip(label_, std::max(0, bounds_.w - 4));
        std::u32string text = U"[ " + label + U" ]";
        if (static_cast<int>(text.size()) > bounds_.w) text = text::clip(text, bounds_.w);
        int cx = bounds_.x + std::max(0, (bounds_.w - static_cast<int>(text.size())) / 2);
        screen.putText(cx, bounds_.y + bounds_.h / 2, text, fg, bg, style);
    }

    bool activate() override {
        if (!enabled_) return false;
        if (onActivate) onActivate();
        return true;
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (!enabled_) return KeyEventResult::Ignored;
        if (ev.key == Key::Enter) return activate() ? KeyEventResult::Activated : KeyEventResult::Ignored;
        if (ev.key == Key::Char && !ev.ctrl && ev.character == U' ')
            return activate() ? KeyEventResult::Activated : KeyEventResult::Ignored;
        return KeyEventResult::Ignored;
    }

private:
    std::u32string label_;
};

} // namespace fenrir
