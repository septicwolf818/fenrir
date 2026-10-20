#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Generic list with selection, scrolling, and an optional header +
// right-hand "detail" column (e.g. file size). The single home of the whole
// "header + rows + selection + scroll" pattern — replaces the hand-drawn
// lists in FileDialog and the file manager.
class ListView : public Widget {
public:
    struct Item {
        std::u32string text;    // main column
        std::u32string detail;  // right column (optional)
        bool dim = false;       // dimmed row (e.g. "..")
        // Disabled (inactive) row: drawn dimmed, skipped by Up/Down/Home/End
        // navigation, and Enter/Space does nothing on it (e.g. settings that
        // only apply while their feature is switched on). Defaults to enabled.
        bool enabled = true;
    };

    std::function<void(int index)> onActivate; // Enter/Space on the selection

    void setItems(std::vector<Item> items) {
        items_ = std::move(items);
        if (selected_ >= static_cast<int>(items_.size())) selected_ = static_cast<int>(items_.size()) - 1;
        if (selected_ < 0) selected_ = 0;
        snapToEnabled();
    }
    void setHeader(std::u32string h) { header_ = std::move(h); }

    const std::vector<Item>& items() const { return items_; }
    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int idx) {
        selected_ = idx;
        clampSelection();
        snapToEnabled();
    }
    int scrollOffset() const { return scroll_; }
    bool empty() const { return items_.empty(); }
    int count() const { return static_cast<int>(items_.size()); }

    const Item* selectedItem() const {
        if (items_.empty() || selected_ < 0 || selected_ >= static_cast<int>(items_.size())) return nullptr;
        return &items_[static_cast<std::size_t>(selected_)];
    }

    // Moves the selection by delta (wrapping), skipping disabled rows.
    // Returns the new index. A list with only disabled rows keeps its index.
    int moveSelection(int delta) {
        if (items_.empty()) return -1;
        clampSelection();
        const int n = static_cast<int>(items_.size());
        for (int step = 0; step < n; ++step) {
            selected_ = ((selected_ + delta) % n + n) % n;
            if (items_[static_cast<std::size_t>(selected_)].enabled) break;
        }
        ensureVisible();
        return selected_;
    }

    // Scrolls so the selected item is visible in the bounds_.h viewport.
    // Horizontally: scrolls ONLY the selected row (its text may be longer
    // than the column) — other rows are drawn from the start, so nothing is lost.
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
        scrollX_ = len <= textW ? 0 : len - textW; // end of the selected text always visible
    }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        int y = bounds_.y;
        if (!header_.empty()) {
            // Focused widget = header in the accent color (clearly marks the active panel).
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
            if (!sel && it.dim) fg = t.dimText; // dimmed row (e.g. "..")
            uint8_t style = sel ? StyleBold : StyleNone;
            if (!it.enabled) { fg = t.dimText; style = StyleNone; } // inactive row color is theme-controlled

            screen.fillRect(Rect{bounds_.x, y + row, bounds_.w, 1}, Cell{U' ', fg, bg, style});

            // Detail column pinned to the right — the main text is clipped before it.
            // A detail longer than the row must not spill past the widget (it would
            // print over the window border) — clipped to w-1, leaving room for the text.
            int detailW = std::min(static_cast<int>(it.detail.size()),
                                   std::max(0, bounds_.w - 1));
            int textW = detailW > 0 ? std::max(0, bounds_.w - detailW - 1) : bounds_.w;
            if (detailW > 0) {
                int dx = bounds_.x + bounds_.w - detailW;
                screen.putText(dx, y + row, text::clip(it.detail, detailW), fg, bg, style);
            }
            // Only the selected row is scrolled horizontally (scrollX_);
            // the rest is displayed from the first character.
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
                if (!items_.empty()) {
                    selected_ = 0;
                    snapToEnabled();
                    ensureVisible();
                }
                return KeyEventResult::Consumed;
            case Key::End:
                if (!items_.empty()) {
                    selected_ = static_cast<int>(items_.size()) - 1;
                    snapToEnabledReverse();
                    ensureVisible();
                }
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
        clampSelection();
        // Enter/Space on a disabled row does nothing (still Consumed by handleKey).
        if (!items_[static_cast<std::size_t>(selected_)].enabled) return false;
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

    // Moves the selection forward (wrapping) to the first enabled row.
    // A list with only disabled rows keeps its index.
    void snapToEnabled() {
        if (items_.empty()) return;
        clampSelection();
        const int n = static_cast<int>(items_.size());
        for (int step = 0; step < n; ++step) {
            if (items_[static_cast<std::size_t>(selected_)].enabled) return;
            selected_ = (selected_ + 1) % n;
        }
    }

    // Same, scanning backward (for End).
    void snapToEnabledReverse() {
        if (items_.empty()) return;
        clampSelection();
        const int n = static_cast<int>(items_.size());
        for (int step = 0; step < n; ++step) {
            if (items_[static_cast<std::size_t>(selected_)].enabled) return;
            selected_ = ((selected_ - 1) % n + n) % n;
        }
    }

    std::vector<Item> items_;
    std::u32string header_;
    int selected_ = 0;
    int scroll_ = 0;
    int scrollX_ = 0; // horizontal offset of ONLY the selected row
};

} // namespace fenrir
