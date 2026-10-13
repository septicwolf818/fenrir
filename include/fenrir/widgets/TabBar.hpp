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
// rysuje karty "etykieta[*]" z podswietleniem biezacej (accentBg + bold);
// przewija poziomo tak, zeby zaznaczona karta byla widoczna, a przy
// przepełnieniu rysuje <> na krawędziach (klik = scrollBy o jedna karte).
// Przelaczanie strzalkami L/P (gdy widget ma fokus), skrotami aplikacji
// (Alt+PgDn/PgUp) albo kliknieciem wirtualnym kursorem (tabAt) — zmiana
// zaznaczenia wola onSwitch(nowyIndex).
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
        ensurePending_ = true; // nowa lista kart — przewin do zaznaczonej
    }

    int selected() const { return selected_; }
    int count() const { return static_cast<int>(tabs_.size()); }

    // Indeks karty pod kolumna ekranu (klik wirtualnym kursorem), -1 gdy
    // kolumna lezy za pasem kart lub na przycisku <>. Uwzglednia przewijanie
    // poziome (scrollX_).
    int tabAt(int screenX) const {
        if (tabs_.empty() || bounds_.w <= 0) return -1;
        if (screenX < bounds_.x || screenX >= bounds_.x + bounds_.w) return -1;
        if (overflow() && (screenX == bounds_.x || screenX == bounds_.x + bounds_.w - 1))
            return -1; // komorki <> sa przyciskami, nie kartami
        const int sx = screenX - bounds_.x + scrollX_;
        int start = 0;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            const int tw = tabWidth(tabs_[static_cast<std::size_t>(i)]);
            if (sx < start + tw) return i;
            start += tw;
        }
        return -1;
    }

    // Klik w przycisk <> (przy przepełnieniu) — przewija pas o jedna karte
    // bez zmiany zaznaczenia; true = trafiony (klik skonsumowany).
    bool scrollHit(int screenX) {
        if (!overflow() || bounds_.w <= 0) return false;
        if (screenX == bounds_.x) { scrollBy(-1); return true; }
        if (screenX == bounds_.x + bounds_.w - 1) { scrollBy(+1); return true; }
        return false;
    }

    // Przewija pas kart o jedna karte w lewo/prawo (przyciski <>).
    void scrollBy(int dir) {
        if (tabs_.empty() || bounds_.w <= 0) return;
        int total = 0;
        for (const auto& t : tabs_) total += tabWidth(t);
        const int maxScroll = std::max(0, total - bounds_.w);
        if (maxScroll == 0) { scrollX_ = 0; return; }
        int sx = std::clamp(scrollX_, 0, maxScroll);
        int ns = sx;
        if (dir > 0) {
            // Do przodu: do poczatku pierwszej karty wystajacej za prawy brzeg.
            int pos = 0;
            for (const auto& t : tabs_) {
                const int tw = tabWidth(t);
                if (pos + tw > sx + bounds_.w) { ns = pos; break; }
                pos += tw;
            }
            if (ns == sx) ns = maxScroll; // nic sie nie zmienilo — skok na koniec
        } else {
            // Do tylu: do poczatku ostatniej karty ukrytej za lewym brzegiem.
            int pos = 0;
            for (const auto& t : tabs_) {
                if (pos < sx) ns = pos;
                pos += tabWidth(t);
            }
            if (ns == sx) ns = 0;
        }
        scrollX_ = std::clamp(ns, 0, maxScroll); // reczne — bez ensureVisible
    }

    // Wymuszone zaznaczenie (po otwarciu/zamknieciu karty) — bez callbacku.
    void select(int idx) {
        selected_ = idx;
        clampSelected();
        ensurePending_ = true; // zmiana zaznaczenia — przewin do nowej karty
    }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.menuText, t.menuBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0 || tabs_.empty()) return;
        // ensureVisible tylko przy zmianie zaznaczenia/rozmiaru — reczne
        // przewijanie (scrollBy) ma zostac tak, jak ustawil uzytkownik.
        if (ensurePending_ || bounds_.x != lastBounds_.x || bounds_.y != lastBounds_.y ||
            bounds_.w != lastBounds_.w || bounds_.h != lastBounds_.h) {
            ensureVisible();
            ensurePending_ = false;
            lastBounds_ = bounds_;
        }

        // Przy przepełnieniu: <> na krawędziach — sygnalizują ukryte karty
        // i przewijaja (klik wirtualnym kursorem: scrollHit).
        const bool ov = overflow();
        const int areaX0 = ov ? bounds_.x + 1 : bounds_.x;
        const int areaX1 = ov ? bounds_.x + bounds_.w - 1 : bounds_.x + bounds_.w;

        int x = bounds_.x - scrollX_;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            const Tab& tab = tabs_[static_cast<std::size_t>(i)];
            const int tw = tabWidth(tab);
            if (x < areaX1 && x + tw > areaX0) {
                const bool sel = (i == selected_);
                const Color bg = sel ? t.accentBg : t.menuBg;
                const Color fg = sel ? t.accentText : t.menuText;
                const uint8_t st = sel ? StyleBold : StyleNone;
                // karta rysowana tylko w granicach widocznego zakresu
                const int cx0 = std::max(x, areaX0);
                const int cx1 = std::min(x + tw, areaX1);
                if (cx1 > cx0)
                    screen.fillRect(Rect{cx0, bounds_.y, cx1 - cx0, 1}, Cell{U' ', fg, bg, st});
                // etykieta/gwiazdka przyciete znak po znaku (nie moga wychodzic
                // poza pas kart — kiedys wychodzily w ramke okna)
                const auto putClipped = [&](int cx, char32_t ch) {
                    if (cx >= areaX0 && cx < areaX1)
                        screen.set(cx, bounds_.y, Cell{ch, fg, bg, st});
                };
                for (std::size_t c = 0; c < tab.label.size(); ++c)
                    putClipped(x + 1 + static_cast<int>(c), tab.label[c]);
                if (tab.dirty) putClipped(x + tw - 2, U'*');
            }
            x += tw;
        }
        if (ov) {
            screen.set(bounds_.x, bounds_.y, Cell{U'<', t.menuText, t.menuBg, StyleBold});
            screen.set(bounds_.x + bounds_.w - 1, bounds_.y, Cell{U'>', t.menuText, t.menuBg, StyleBold});
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

    // Czy karty nie mieszcza sie w pasku (rysujemy wtedy <> i przewijamy).
    bool overflow() const {
        if (tabs_.empty() || bounds_.w <= 0) return false;
        int total = 0;
        for (const auto& t : tabs_) total += tabWidth(t);
        return total > bounds_.w;
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
    bool ensurePending_ = true; // czy przewinac do zaznaczonej przy nastepnym draw
    Rect lastBounds_{};         // poprzedni rozmiar (resize = ponowne ensureVisible)
};

} // namespace fenrir
