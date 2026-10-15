#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Generic single-column item picker for keyboard mode: Up/Down moves the
// selection (the viewport scrolls to follow it), Enter/Space invokes
// onSelect with the selected index. Long rows are clipped to the widget
// width (no wrapping, no horizontal scrolling). Non-editable; deliberately
// minimal — no detail column, no per-row states (see ListView for the
// full-featured list, TextView for plain scrollable text).
// At the edges (empty list, or already at the first/last row) handleKey
// returns Ignored instead of Consumed, so an outer container can reuse
// the key for something else.
class SelectionList : public Widget {
public:
    // Called with the selected index on Enter/Space. Unset = selection only.
    std::function<void(int index)> onSelect;

    void setItems(std::vector<std::u32string> items) {
        items_ = std::move(items);
        if (selected_ >= count()) selected_ = count() - 1;
        if (selected_ < 0) selected_ = 0;
        clampScroll();
    }

    const std::vector<std::u32string>& items() const { return items_; }
    int count() const { return static_cast<int>(items_.size()); }
    bool empty() const { return items_.empty(); }

    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int idx) {
        if (items_.empty()) { selected_ = 0; return; }
        selected_ = std::clamp(idx, 0, count() - 1);
        ensureVisible();
    }

    const std::u32string* selectedItem() const {
        if (items_.empty() || selected_ < 0 || selected_ >= count()) return nullptr;
        return &items_[static_cast<std::size_t>(selected_)];
    }

    // Moves the selection by delta, clamped to the ends (no wrapping).
    // Returns the new index (-1 when empty).
    int moveSelection(int delta) {
        if (items_.empty()) return -1;
        setSelectedIndex(selected_ + delta);
        return selected_;
    }

    int scrollOffset() const { return scroll_; }

    bool editable() const override { return false; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;
        clampScroll();
        for (int row = 0; row < bounds_.h; ++row) {
            const int idx = scroll_ + row;
            if (idx >= count()) break;
            const bool sel = idx == selected_;
            Color fg = sel ? t.accentText : t.surfaceText;
            Color bg = sel ? t.accentBg : t.surfaceBg;
            uint8_t style = sel ? StyleBold : StyleNone;
            if (!enabled_) { fg = t.dimText; style = StyleDim; } // inactive list
            screen.fillRect(Rect{bounds_.x, bounds_.y + row, bounds_.w, 1},
                            Cell{U' ', fg, bg, style});
            screen.putText(bounds_.x, bounds_.y + row,
                           text::clip(items_[static_cast<std::size_t>(idx)], bounds_.w),
                           fg, bg, style);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (!enabled_) return KeyEventResult::Ignored;
        if (items_.empty()) return KeyEventResult::Ignored;
        const int page = std::max(1, bounds_.h);
        const int before = selected_;
        switch (ev.key) {
            case Key::Up:       moveSelection(-1);    break;
            case Key::Down:     moveSelection(+1);    break;
            case Key::PageUp:   moveSelection(-page); break;
            case Key::PageDown: moveSelection(+page); break;
            case Key::Home:     setSelectedIndex(0);          break;
            case Key::End:      setSelectedIndex(count() - 1); break;
            case Key::Enter:    return activate() ? KeyEventResult::Activated
                                                  : KeyEventResult::Ignored;
            case Key::Char:
                if (ev.ctrl || ev.alt || ev.character != U' ') return KeyEventResult::Ignored;
                return activate() ? KeyEventResult::Activated : KeyEventResult::Ignored;
            default: return KeyEventResult::Ignored;
        }
        return selected_ != before ? KeyEventResult::Consumed : KeyEventResult::Ignored;
    }

    bool activate() override {
        if (!enabled_ || items_.empty()) return false;
        if (selected_ < 0 || selected_ >= count()) return false;
        if (onSelect) onSelect(selected_);
        return true;
    }

private:
    int maxScroll() const {
        return std::max(0, count() - std::max(0, bounds_.h));
    }

    void clampScroll() { scroll_ = std::clamp(scroll_, 0, maxScroll()); }

    // Scrolls so the selected item is visible in the bounds_.h viewport.
    void ensureVisible() {
        const int rows = std::max(0, bounds_.h);
        if (rows <= 0 || items_.empty()) return;
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + rows) scroll_ = selected_ - rows + 1;
        clampScroll();
    }

    std::vector<std::u32string> items_;
    int selected_ = 0;
    int scroll_ = 0; // first visible row
};

} // namespace fenrir
