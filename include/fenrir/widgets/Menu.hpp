#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Selection list (also the basic dropdown menu). Rows on surface,
// selection on accent — exactly like in file lists.
class Menu : public Widget {
public:
    explicit Menu(std::vector<std::u32string> items) : items_(std::move(items)) {}

    std::function<void(int index)> onSelect;

    int selectedIndex() const { return selected_; }
    void setSelectedIndex(int i) { selected_ = i; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (bounds_.h <= 0 || items_.empty()) return;
        ensureVisible();

        const int maxW = std::max(0, bounds_.w - 1);
        const int count = static_cast<int>(items_.size());
        for (int i = scroll_; i < count && (i - scroll_) < bounds_.h; ++i) {
            const bool sel = i == selected_;
            const Color fg = sel ? t.accentText : t.surfaceText;
            const Color bg = sel ? t.accentBg : t.surfaceBg;
            const uint8_t style = sel ? StyleBold : StyleNone;
            const int y = bounds_.y + (i - scroll_);
            // Text clipped + the row padded with the same background (full highlight).
            screen.fillRect(Rect{bounds_.x, y, bounds_.w, 1}, Cell{U' ', fg, bg, style});
            screen.putText(bounds_.x + 1, y, text::clip(items_[static_cast<std::size_t>(i)], maxW), fg, bg, style);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (items_.empty()) return KeyEventResult::Ignored;
        const int n = static_cast<int>(items_.size());
        if (ev.key == Key::Up) {
            selected_ = (selected_ - 1 + n) % n;
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Down) {
            selected_ = (selected_ + 1) % n;
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::PageUp) {
            selected_ = std::max(0, selected_ - std::max(1, bounds_.h));
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::PageDown) {
            selected_ = std::min(n - 1, selected_ + std::max(1, bounds_.h));
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Home) {
            selected_ = 0;
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::End) {
            selected_ = n - 1;
            ensureVisible();
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
    // Scrolls so the selected item is visible — otherwise a large menu
    // (more items than bounds_.h rows) would have its selection off screen.
    void ensureVisible() {
        if (bounds_.h <= 0 || items_.empty()) return;
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + bounds_.h) scroll_ = selected_ - bounds_.h + 1;
        const int maxScroll = std::max(0, static_cast<int>(items_.size()) - bounds_.h);
        scroll_ = std::clamp(scroll_, 0, maxScroll);
    }

    std::vector<std::u32string> items_;
    int selected_ = 0;
    int scroll_ = 0;
};

} // namespace fenrir
