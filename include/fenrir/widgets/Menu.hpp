#pragma once
#include <functional>
#include <string>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Lista wyboru (tez podstawowe menu rozwijane). Wiersze na surface,
// zaznaczenie na accent — dokladnie jak w listach plikow.
class Menu : public Widget {
public:
    explicit Menu(std::vector<std::u32string> items) : items_(std::move(items)) {}

    std::function<void(int index)> onSelect;

    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int i) { selected_ = i; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});

        const int maxW = std::max(0, bounds_.w - 1);
        for (std::size_t i = 0; i < items_.size() && static_cast<int>(i) < bounds_.h; ++i) {
            const bool sel = static_cast<int>(i) == selected_;
            const Color fg = sel ? t.accentText : t.surfaceText;
            const Color bg = sel ? t.accentBg : t.surfaceBg;
            const uint8_t style = sel ? StyleBold : StyleNone;
            const int y = bounds_.y + static_cast<int>(i);
            // Tekst przyciety + dopełnienie wiersza tym samym tlem (pelne podswietlenie).
            screen.fillRect(Rect{bounds_.x, y, bounds_.w, 1}, Cell{U' ', fg, bg, style});
            screen.putText(bounds_.x + 1, y, text::clip(items_[i], maxW), fg, bg, style);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (items_.empty()) return KeyEventResult::Ignored;
        if (ev.key == Key::Up) {
            selected_ = (selected_ - 1 + static_cast<int>(items_.size())) % static_cast<int>(items_.size());
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Down) {
            selected_ = (selected_ + 1) % static_cast<int>(items_.size());
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Enter)
            return activate() ? KeyEventResult::Activated : KeyEventResult::Ignored;
        return KeyEventResult::Ignored;
    }

    bool activate() override {
        if (items_.empty()) return false;
        if (onSelect) onSelect(selected_);
        return true;
    }

private:
    std::vector<std::u32string> items_;
    int selected_ = 0;
};

} // namespace fenrir
