#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Multiline read-only text with vertical scrolling. The buffer stores one
// std::u32string per line (no '\n' inside); long lines are clipped to the
// widget width (no wrapping, no horizontal scrolling) — each logical line
// is exactly one visual row, so every entry (e.g. a calendar agenda event)
// stays on its own line.
// Scrolling: scrollBy/scrollTo/scrollToTop/scrollToBottom, plus Up/Down/
// PageUp/PageDown/Home/End in handleKey. At the edges handleKey returns
// Ignored (instead of Consumed) so an outer container can reuse the key.
class TextView : public Widget {
public:
    TextView() = default;

    explicit TextView(std::u32string text) { setText(std::move(text)); }

    // Replaces the content, splitting on '\n' (a trailing '\r' is stripped,
    // so CRLF behaves like LF). The scroll offset is clamped; call
    // scrollToTop() when switching to unrelated content (e.g. another day).
    void setText(std::u32string text) {
        lines_.clear();
        std::size_t start = 0;
        const std::size_t n = text.size();
        for (std::size_t i = 0; i <= n; ++i) {
            if (i == n || text[i] == U'\n') {
                std::u32string line = std::u32string(text.substr(start, i - start));
                if (!line.empty() && line.back() == U'\r') line.pop_back();
                lines_.push_back(std::move(line));
                start = i + 1;
            }
        }
        clampScroll();
    }

    void setLines(std::vector<std::u32string> lines) {
        lines_ = std::move(lines);
        clampScroll();
    }

    const std::vector<std::u32string>& lines() const { return lines_; }
    int lineCount() const { return static_cast<int>(lines_.size()); }
    bool empty() const { return lines_.empty(); }

    void setColors(Color fg, Color bg) { fg_ = fg; bg_ = bg; }
    void setBold(bool on) { bold_ = on; }

    // --- Scrolling -------------------------------------------------------
    int scrollOffset() const { return scroll_; }
    int maxScroll() const {
        return std::max(0, static_cast<int>(lines_.size()) - std::max(0, bounds_.h));
    }
    bool canScrollUp() const { return scroll_ > 0; }
    bool canScrollDown() const { return scroll_ < maxScroll(); }

    void scrollTo(int offset) {
        scroll_ = std::clamp(offset, 0, maxScroll());
    }
    // Returns the new offset (clamped).
    int scrollBy(int delta) {
        scrollTo(scroll_ + delta);
        return scroll_;
    }
    void scrollToTop() { scroll_ = 0; }
    void scrollToBottom() { scroll_ = maxScroll(); }

    bool editable() const override { return false; }

    void draw(Screen& screen) override {
        screen.fillRect(bounds_, Cell{U' ', fg_, bg_, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;
        clampScroll();
        const uint8_t style = bold_ ? StyleBold : StyleNone;
        for (int row = 0; row < bounds_.h; ++row) {
            const int idx = scroll_ + row;
            if (idx >= static_cast<int>(lines_.size())) break;
            screen.putText(bounds_.x, bounds_.y + row,
                           text::clip(lines_[static_cast<std::size_t>(idx)], bounds_.w),
                           fg_, bg_, style);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (!enabled_) return KeyEventResult::Ignored;
        const int page = std::max(1, bounds_.h);
        const int before = scroll_;
        switch (ev.key) {
            case Key::Up:       scrollBy(-1);    break;
            case Key::Down:      scrollBy(+1);    break;
            case Key::PageUp:   scrollBy(-page); break;
            case Key::PageDown: scrollBy(+page); break;
            case Key::Home:     scrollToTop();    break;
            case Key::End:      scrollToBottom(); break;
            default: return KeyEventResult::Ignored;
        }
        return scroll_ != before ? KeyEventResult::Consumed : KeyEventResult::Ignored;
    }

    bool selectable() const override { return enabled_; }

private:
    void clampScroll() { scroll_ = std::clamp(scroll_, 0, maxScroll()); }

    std::vector<std::u32string> lines_;
    int scroll_ = 0; // first visible line
    Color fg_ = theme::active.windowText;
    Color bg_ = theme::active.windowBg;
    bool bold_ = false;
};

} // namespace fenrir
