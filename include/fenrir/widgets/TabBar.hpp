#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Pasek kart (tabs) — np. otwarte pliki w edytorze. Widget biblioteczny:
// rysuje karty "etykieta[*]" z podswietleniem biezacej (accentBg + bold),
// przewija poziomo tak, by zaznaczona karta byla widoczna. Przełączanie
// strzalkami L/P (gdy widget ma fokus) albo skrotami aplikacji (F4/F9) —
// zmiana zaznaczenia wolaa onSwitch(nowyIndex).
class TabBar : public Widget {
public:
    struct Tab {
        std::u32string label;
        bool dirty = false; // gwiazdka za etykieta
    };

    std::function<void(int)> onSwitch;

    void setTabs(std::vector<Tab> tabs) {
        tabs_ = std::move(tabs);
        clampSelected();
    }

    int selected() const { return selected_; }
    int count() const { return static_cast<int>(tabs_.size()); }

    // Wymuszone zaznaczenie (po otwarciu/zamknieciu karty) — bez callbacku.
    void select(int idx) {
        selected_ = idx;
        clampSelected();
    }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.menuText, t.menuBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0 || tabs_.empty()) return;
        ensureVisible();

        int x = bounds_.x - scrollX_;
        const int end = bounds_.x + bounds_.w;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            const Tab& tab = tabs_[static_cast<std::size_t>(i)];
            const int tw = tabWidth(tab);
            if (x < end && x + tw > bounds_.x) {
                const bool sel = (i == selected_);
                const Color bg = sel ? t.accentBg : t.menuBg;
                const Color fg = sel ? t.accentText : t.menuText;
                const uint8_t st = sel ? StyleBold : StyleNone;
                // karta rysowana tylko w granicach paska (przycięcie przy krawędzi)
                const int cx0 = std::max(x, bounds_.x);
                const int cx1 = std::min(x + tw, end);
                if (cx1 > cx0)
                    screen.fillRect(Rect{cx0, bounds_.y, cx1 - cx0, 1}, Cell{U' ', fg, bg, st});
                if (x + 1 >= bounds_.x && x + 1 < end)
                    screen.putText(x + 1, bounds_.y, tab.label, fg, bg, st);
                if (tab.dirty && x + tw - 2 >= bounds_.x && x + tw - 2 < end)
                    screen.putText(x + tw - 2, bounds_.y, U"*", fg, bg, st);
            }
            x += tw;
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        switch (ev.key) {
            case Key::Left:   step(-1); return KeyEventResult::Consumed;
            case Key::Right:  step(+1); return KeyEventResult::Consumed;
            case Key::Home:   select(0); fire(); return KeyEventResult::Consumed;
            case Key::End:    select(count() - 1); fire(); return KeyEventResult::Consumed;
            default: return KeyEventResult::Ignored;
        }
    }

    bool selectable() const override { return true; }

private:
    static int tabWidth(const Tab& t) {
        return static_cast<int>(t.label.size()) + (t.dirty ? 1 : 0) + 2; // spacje + '*'
    }

    void clampSelected() {
        if (tabs_.empty()) { selected_ = 0; return; }
        selected_ = std::clamp(selected_, 0, static_cast<int>(tabs_.size()) - 1);
    }

    void step(int d) {
        if (tabs_.empty()) return;
        const int n = static_cast<int>(tabs_.size());
        select(((selected_ + d) % n + n) % n); // cyklicznie
        fire();
    }

    void fire() {
        if (onSwitch) onSwitch(selected_);
    }

    // Przesuwa scrollX_, zeby zaznaczona karta lezala w widocznym zakresie.
    void ensureVisible() {
        if (tabs_.empty() || bounds_.w <= 0) { scrollX_ = 0; return; }
        int start = 0;
        for (int i = 0; i < selected_; ++i)
            start += tabWidth(tabs_[static_cast<std::size_t>(i)]);
        const int end = start + tabWidth(tabs_[static_cast<std::size_t>(selected_)]);
        if (start < scrollX_) scrollX_ = start;
        if (end > scrollX_ + bounds_.w) scrollX_ = end - bounds_.w;
        scrollX_ = std::max(0, scrollX_);
    }

    std::vector<Tab> tabs_;
    int selected_ = 0;
    int scrollX_ = 0;
};

} // namespace fenrir
