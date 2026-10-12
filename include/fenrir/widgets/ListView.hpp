#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Genericzna lista z zaznaczeniem, przewijaniem i opcjonalnym naglowkiem +
// prawa kolumna "detail" (np. rozmiar pliku). Jedno miejsce na caly wzorzec
// "naglowek + wiersze + zaznaczenie + scroll" — zastepuje recznie rysowane
// listy w FileDialogu i menedzerze plikow.
class ListView : public Widget {
public:
    struct Item {
        std::u32string text;    // glowna kolumna
        std::u32string detail;  // prawa kolumna (opcjonalna)
        bool dim = false;       // wyszarzony wiersz (np. "..")
    };

    std::function<void(int index)> onActivate; // Enter/Space na zaznaczonym

    void setItems(std::vector<Item> items) {
        items_ = std::move(items);
        if (selected_ >= static_cast<int>(items_.size())) selected_ = static_cast<int>(items_.size()) - 1;
        if (selected_ < 0) selected_ = 0;
    }
    void setHeader(std::u32string h) { header_ = std::move(h); }

    const std::vector<Item>& items() const { return items_; }
    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int idx) {
        selected_ = idx;
        clampSelection();
    }
    int scrollOffset() const { return scroll_; }
    bool empty() const { return items_.empty(); }
    int count() const { return static_cast<int>(items_.size()); }

    const Item* selectedItem() const {
        if (items_.empty() || selected_ < 0 || selected_ >= static_cast<int>(items_.size())) return nullptr;
        return &items_[static_cast<std::size_t>(selected_)];
    }

    // Przesuwa zaznaczenie o delta (z owijaniem). Zwraca nowy indeks.
    int moveSelection(int delta) {
        if (items_.empty()) return -1;
        clampSelection();
        const int n = static_cast<int>(items_.size());
        selected_ = ((selected_ + delta) % n + n) % n;
        ensureVisible();
        return selected_;
    }

    // Przewija tak, zeby zaznaczony element byl widoczny w viewport bounds_.h.
    // Poziomo: przewija TYLKO zaznaczony wiersz (jego tekst moze byc dluzszy
    // niz kolumna) — pozostale wiersze odmeruwaja od poczatku, wiec nic nie giniemy.
    void ensureVisible() {
        const int rows = viewportRows();
        if (rows <= 0 || items_.empty()) return;
        clampSelection();
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + rows) scroll_ = selected_ - rows + 1;
        const int maxScroll = std::max(0, static_cast<int>(items_.size()) - rows);
        scroll_ = std::clamp(scroll_, 0, maxScroll);

        const Item& it = items_[static_cast<std::size_t>(selected_)];
        const int detailW = static_cast<int>(it.detail.size());
        const int textW = detailW > 0 ? std::max(0, bounds_.w - detailW - 1) : bounds_.w;
        const int len = static_cast<int>(it.text.size());
        scrollX_ = len <= textW ? 0 : len - textW; // koniec zaznaczonego tekstu zawsze widoczny
    }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        int y = bounds_.y;
        if (!header_.empty()) {
            // Fokusowany widget = naglowek na kolorze akcentu (wyrazny aktywny panel).
            const Color hb = focused_ ? t.accentBg : t.headerBg;
            const Color ht = focused_ ? t.accentText : t.headerText;
            screen.fillRect(Rect{bounds_.x, y, bounds_.w, 1}, Cell{U' ', ht, hb, StyleBold});
            screen.putText(bounds_.x, y, text::clip(header_, bounds_.w), ht, hb, StyleBold);
            ++y;
        }

        const int rows = viewportRows();
        ensureVisible();
        for (int row = 0; row < rows; ++row) {
            const int idx = scroll_ + row;
            if (idx >= static_cast<int>(items_.size())) break;
            const Item& it = items_[static_cast<std::size_t>(idx)];
            const bool sel = idx == selected_;

            Color fg = sel ? t.accentText : t.surfaceText;
            Color bg = sel ? t.accentBg : t.surfaceBg;
            if (!sel && it.dim) fg = t.dimText; // przygaszony wiersz (np. "..")
            const uint8_t style = sel ? StyleBold : StyleNone;

            screen.fillRect(Rect{bounds_.x, y + row, bounds_.w, 1}, Cell{U' ', fg, bg, style});

            // Kolumna detail przypieta do prawej — tekst glowny ucina sie przed nia.
            int detailW = static_cast<int>(it.detail.size());
            int textW = detailW > 0 ? std::max(0, bounds_.w - detailW - 1) : bounds_.w;
            if (detailW > 0) {
                int dx = bounds_.x + bounds_.w - detailW;
                screen.putText(dx, y + row, text::clip(it.detail, detailW), fg, bg, style);
            }
            // Tylko zaznaczony wiersz jest przewiniety poziomo (scrollX_),
            // reszta wyswietlana od pierwszego znaku.
            std::u32string_view tv = it.text;
            const int off = sel ? std::min(scrollX_, std::max(0, static_cast<int>(tv.size()) - 1)) : 0;
            if (off > 0) tv.remove_prefix(static_cast<std::size_t>(off));
            screen.putText(bounds_.x, y + row, text::clip(tv, textW), fg, bg, style);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        const int rows = std::max(1, viewportRows());
        switch (ev.key) {
            case Key::Up:      moveSelection(-1); return KeyEventResult::Consumed;
            case Key::Down:    moveSelection(+1); return KeyEventResult::Consumed;
            case Key::PageUp:   moveSelection(-rows); return KeyEventResult::Consumed;
            case Key::PageDown: moveSelection(rows); return KeyEventResult::Consumed;
            case Key::Home:
                if (!items_.empty()) { selected_ = 0; ensureVisible(); }
                return KeyEventResult::Consumed;
            case Key::End:
                if (!items_.empty()) { selected_ = static_cast<int>(items_.size()) - 1; ensureVisible(); }
                return KeyEventResult::Consumed;
            case Key::Enter:
            case Key::Char:
                if (ev.key == Key::Char && (ev.ctrl || ev.character != U' ')) return KeyEventResult::Ignored;
                return activate() ? KeyEventResult::Activated : KeyEventResult::Consumed;
            default:
                return KeyEventResult::Ignored;
        }
    }

    bool activate() override {
        if (items_.empty()) return false;
        if (onActivate) onActivate(selected_);
        return true;
    }

private:
    int viewportRows() const {
        const int headerRows = header_.empty() ? 0 : 1;
        return std::max(0, bounds_.h - headerRows);
    }

    void clampSelection() {
        if (items_.empty()) { selected_ = 0; return; }
        selected_ = std::clamp(selected_, 0, static_cast<int>(items_.size()) - 1);
    }

    std::vector<Item> items_;
    std::u32string header_;
    int selected_ = 0;
    int scroll_ = 0;
    int scrollX_ = 0; // przesuniecie poziome TYLKO zaznaczonego wiersza
};

} // namespace fenrir
