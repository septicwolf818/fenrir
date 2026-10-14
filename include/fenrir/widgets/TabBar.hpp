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

// Tab bar — e.g. open files in an editor. This library widget draws tabs as
// "label[*]", highlighting the current tab (accentBg + bold); it scrolls
// horizontally to keep the selected tab visible and draws <> at the edges
// when tabs overflow (clicking scrolls by one tab via scrollBy).
// Switch tabs with Left/Right (when focused), app shortcuts (Alt+PgDn/PgUp),
// or a virtual-cursor click (tabAt); selection changes invoke onSwitch(newIndex).
class TabBar : public Widget {
public:
    struct Tab {
        std::u32string label;
        bool dirty = false; // Show an asterisk after the label.
    };

    std::function<void(int)> onSwitch;

    void setTabs(std::vector<Tab> tabs) {
        tabs_ = std::move(tabs);
        clampSelected();
        ensurePending_ = true; // New tab list — scroll to the selected tab.
    }

    // Update a dirty marker without forcing the current tab back into view.
    void setDirty(int index, bool dirty) {
        if (index < 0 || index >= count()) return;
        tabs_[static_cast<std::size_t>(index)].dirty = dirty;
        scrollX_ = std::clamp(scrollX_, 0, maxScroll());
    }

    int selected() const { return selected_; }
    int count() const { return static_cast<int>(tabs_.size()); }

    // Index of the tab under a screen column (virtual-cursor click); -1 if the
    // column is outside the tab bar or on a <> button. Accounts for horizontal
    // scrolling (scrollX_).
    int tabAt(int screenX) const {
        if (tabs_.empty() || bounds_.w <= 0) return -1;
        if (screenX < bounds_.x || screenX >= bounds_.x + bounds_.w) return -1;
        if (overflow() && (screenX == bounds_.x || screenX == bounds_.x + bounds_.w - 1))
            return -1; // The <> cells are buttons, not tabs.
        const int sx = screenX - contentStartX() + scrollX_;
        int start = 0;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            const int tw = tabWidth(tabs_[static_cast<std::size_t>(i)]);
            if (sx < start + tw) return i;
            start += tw;
        }
        return -1;
    }

    // Click a <> button (when tabs overflow) to scroll by one tab without
    // changing the selection; true means the button was hit (click consumed).
    bool scrollHit(int screenX) {
        if (!overflow() || bounds_.w <= 0) return false;
        if (screenX == bounds_.x) { scrollBy(-1); return true; }
        if (screenX == bounds_.x + bounds_.w - 1) { scrollBy(+1); return true; }
        return false;
    }

    // Scroll the tab bar one tab left or right (<> buttons).
    void scrollBy(int dir) {
        if (tabs_.empty() || bounds_.w <= 0) return;
        const int max = maxScroll();
        if (max == 0) { scrollX_ = 0; return; }
        int sx = std::clamp(scrollX_, 0, max);
        const int viewW = visibleWidth();
        if (viewW <= 0) return;
        int ns = sx;
        if (dir > 0) {
            // Forward: to the start of the first tab extending past the right edge.
            int pos = 0;
            for (const auto& t : tabs_) {
                const int tw = tabWidth(t);
                if (pos + tw > sx + viewW) { ns = pos; break; }
                pos += tw;
            }
            if (ns == sx) ns = max; // No change — jump to the end.
        } else {
            // Backward: to the start of the last tab hidden past the left edge.
            int pos = 0;
            for (const auto& t : tabs_) {
                if (pos < sx) ns = pos;
                pos += tabWidth(t);
            }
            if (ns == sx) ns = 0;
        }
        scrollX_ = std::clamp(ns, 0, max); // Manual scroll — do not call ensureVisible.
    }

    // Force selection (after opening/closing a tab) without invoking the callback.
    void select(int idx) {
        selected_ = idx;
        clampSelected();
        ensurePending_ = true; // Selection changed — scroll to the new tab.
    }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.menuText, t.menuBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0 || tabs_.empty()) return;
        // Call ensureVisible only when the selection or size changes; preserve
        // manual scrolling (scrollBy) as set by the user.
        if (ensurePending_ || bounds_.x != lastBounds_.x || bounds_.y != lastBounds_.y ||
            bounds_.w != lastBounds_.w || bounds_.h != lastBounds_.h) {
            ensureVisible();
            ensurePending_ = false;
            lastBounds_ = bounds_;
        }

        // When tabs overflow, <> at the edges indicate hidden tabs and scroll
        // the bar when clicked with the virtual cursor (scrollHit).
        const bool ov = overflow();
        const int areaX0 = ov ? bounds_.x + 1 : bounds_.x;
        const int areaX1 = ov ? bounds_.x + bounds_.w - 1 : bounds_.x + bounds_.w;

        int x = areaX0 - scrollX_;
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            const Tab& tab = tabs_[static_cast<std::size_t>(i)];
            const int tw = tabWidth(tab);
            if (x < areaX1 && x + tw > areaX0) {
                const bool sel = (i == selected_);
                const Color bg = sel ? t.accentBg : t.menuBg;
                const Color fg = sel ? t.accentText : t.menuText;
                const uint8_t st = sel ? StyleBold : StyleNone;
                // Draw the tab only within the visible area.
                const int cx0 = std::max(x, areaX0);
                const int cx1 = std::min(x + tw, areaX1);
                if (cx1 > cx0)
                    screen.fillRect(Rect{cx0, bounds_.y, cx1 - cx0, 1}, Cell{U' ', fg, bg, st});
                // Clip the label/asterisk character by character so they cannot
                // extend beyond the tab bar (they once overlapped the window frame).
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

    bool selectable() const override { return enabled_; }

private:
    static int tabWidth(const Tab& t) {
        return static_cast<int>(t.label.size()) + (t.dirty ? 1 : 0) + 2; // Padding + '*'.
    }

    // Do the tabs overflow the bar? If so, draw <> and enable scrolling.
    bool overflow() const {
        if (tabs_.empty() || bounds_.w <= 0) return false;
        int total = 0;
        for (const auto& t : tabs_) total += tabWidth(t);
        return total > bounds_.w;
    }

    int visibleWidth() const { return std::max(0, bounds_.w - (overflow() ? 2 : 0)); }
    int contentStartX() const { return bounds_.x + (overflow() ? 1 : 0); }
    int maxScroll() const {
        int total = 0;
        for (const auto& t : tabs_) total += tabWidth(t);
        return std::max(0, total - visibleWidth());
    }

    void clampSelected() {
        if (tabs_.empty()) { selected_ = 0; return; }
        selected_ = std::clamp(selected_, 0, static_cast<int>(tabs_.size()) - 1);
    }

    void step(int d) {
        if (tabs_.empty()) return;
        const int n = static_cast<int>(tabs_.size());
        select(((selected_ + d) % n + n) % n); // Wrap around.
        fire();
    }

    void fire() {
        if (onSwitch) onSwitch(selected_);
    }

    // Adjust scrollX_ so the selected tab is within the visible area.
    void ensureVisible() {
        if (tabs_.empty() || bounds_.w <= 0 || visibleWidth() <= 0) { scrollX_ = 0; return; }
        const int viewW = visibleWidth();
        int start = 0;
        for (int i = 0; i < selected_; ++i)
            start += tabWidth(tabs_[static_cast<std::size_t>(i)]);
        const int end = start + tabWidth(tabs_[static_cast<std::size_t>(selected_)]);
        if (start < scrollX_) scrollX_ = start;
        if (end > scrollX_ + viewW) scrollX_ = end - viewW;
        scrollX_ = std::max(0, scrollX_);
    }

    std::vector<Tab> tabs_;
    int selected_ = 0;
    int scrollX_ = 0;
    bool ensurePending_ = true; // Scroll to the selected tab on the next draw.
    Rect lastBounds_{};         // Previous bounds (resize triggers ensureVisible again).
};

} // namespace fenrir
