#pragma once
#include <string>
#include <utility>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Pasek statusu / podpowiedzi: tekst po lewej + opcjonalny po prawej.
// Jedno miejsce na wzorzec "szary pasek na dole z instrukcja" — do tej pory
// rysowany recznie w trzech miejscach (dialog, menedzer, podpowiedzi).
class StatusBar : public Widget {
public:
    void setText(std::u32string left, std::u32string right = {}) {
        left_ = std::move(left);
        right_ = std::move(right);
    }
    const std::u32string& text() const { return left_; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.statusText, t.statusBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        screen.putText(bounds_.x, bounds_.y, text::clip(left_, bounds_.w), t.statusText, t.statusBg);

        if (!right_.empty()) {
            std::u32string r = text::clip(right_, bounds_.w);
            int rx = bounds_.x + bounds_.w - static_cast<int>(r.size());
            if (rx > bounds_.x) screen.putText(rx, bounds_.y, r, t.statusText, t.statusBg, StyleBold);
        }
    }

    KeyEventResult handleKey(const KeyEvent&) override { return KeyEventResult::Ignored; }

    bool selectable() const override { return false; }

private:
    std::u32string left_;
    std::u32string right_;
};

} // namespace fenrir
