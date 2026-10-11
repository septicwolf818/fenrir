#pragma once
#include <functional>
#include <string>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

class Menu : public Widget {
public:
    explicit Menu(std::vector<std::u32string> items) : items_(std::move(items)) {}

    std::function<void(int index)> onSelect;

    void draw(Screen& screen) override {
        // Menu rozwijane: White tlo, Black tekst (czytelne ciało).
        // Wybrany element: White tekst na Blue tle, bold — spójne z zaznaczaniem list.
        for (int y = bounds_.y; y < bounds_.y + bounds_.h; ++y)
            for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x)
                screen.set(x, y, Cell{U' ', colors::Black, colors::White, 0});

        for (std::size_t i = 0; i < items_.size() && static_cast<int>(i) < bounds_.h; ++i) {
            bool sel = static_cast<int>(i) == selected_;
            Color fg = sel ? colors::White : colors::Black;
            Color bg = sel ? colors::Blue : colors::White;
            uint8_t style = sel ? StyleBold : StyleNone;
            screen.putText(bounds_.x + 1, bounds_.y + static_cast<int>(i), items_[i], fg, bg, style);
            // dopełnienie reszty wiersza tym samym tłem, żeby podświetlenie było na całą szerokość
            for (int x = bounds_.x + 1 + static_cast<int>(items_[i].size()); x < bounds_.x + bounds_.w; ++x)
                screen.set(x, bounds_.y + static_cast<int>(i), Cell{U' ', fg, bg, style});
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
        if (ev.key == Key::Enter) {
            if (onSelect) onSelect(selected_);
            return KeyEventResult::Activated;
        }
        return KeyEventResult::Ignored;
    }

private:
    std::vector<std::u32string> items_;
    int selected_ = 0;
};

} // namespace fenrir
