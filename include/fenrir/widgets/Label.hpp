#pragma once
#include <algorithm>
#include <string>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Text alignment within the Label's width.
enum class LabelAlign { Left, Center, Right };

// Text (static or changed on the fly via setText()). The background fills the
// whole Label rectangle; the text is aligned per align_ (default Left) and
// clipped to the width (long text does not spill past the widget).
// Colors: theme defaults, overridable via the constructor/setters.
class Label : public Widget {
public:
    explicit Label(std::u32string text,
                   Color fg = theme::active.windowText,
                   Color bg = theme::active.windowBg)
        : text_(std::move(text)), fg_(fg), bg_(bg) {}

    void setText(std::u32string t) { text_ = std::move(t); }
    const std::u32string& text() const { return text_; }
    void setAlignment(LabelAlign a) { align_ = a; }
    void setColors(Color fg, Color bg) { fg_ = fg; bg_ = bg; }

    void draw(Screen& screen) override {
        screen.fillRect(bounds_, Cell{U' ', fg_, bg_, 0});
        std::u32string shown = text::clip(text_, bounds_.w);
        int off = 0;
        int tw = static_cast<int>(shown.size());
        if (tw < bounds_.w) {
            if (align_ == LabelAlign::Center) off = (bounds_.w - tw) / 2;
            else if (align_ == LabelAlign::Right) off = bounds_.w - tw;
        }
        screen.putText(bounds_.x + std::max(0, off), bounds_.y, shown, fg_, bg_);
    }

    KeyEventResult handleKey(const KeyEvent&) override { return KeyEventResult::Ignored; }

private:
    std::u32string text_;
    Color fg_, bg_;
    LabelAlign align_ = LabelAlign::Left;
};

} // namespace fenrir
