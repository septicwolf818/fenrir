#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Container with an ASCII border and a title on the top edge — turns the pattern
// previously drawn by hand in Desktop (drawBox) and windows into a widget.
// The interior is filled with the window color; the child gets bounds inside the border.
class Frame : public Widget {
public:
    explicit Frame(std::u32string title = {}) : title_(std::move(title)) {}

    void setTitle(std::u32string t) { title_ = std::move(t); }
    const std::u32string& title() const { return title_; }

    void setContent(std::unique_ptr<Widget> w) { content_ = std::move(w); }
    Widget* content() const { return content_.get(); }

    // The frame inherits the child's editability — if it is the window's content.
    bool editable() const override { return content_ ? content_->editable() : editable_; }

    // Minima are also inherited from the child (+2 in each dimension for the border).
    int minWidth() const override { return content_ ? content_->minWidth() + 2 : 0; }
    int minHeight() const override { return content_ ? content_->minHeight() + 2 : 0; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        // Interior + border (white for the active window, gray otherwise — like windows).
        const Color line = focused_ ? t.border : t.borderInactive;
        screen.fillRect(bounds_, Cell{U' ', t.windowText, t.windowBg, 0});
        screen.drawBorder(bounds_, U'+', U'-', U'|', line, t.windowBg);

        if (!title_.empty() && bounds_.w > 4) {
            // Title written into the top edge of the border.
            std::u32string label = U" " + title_ + U" ";
            int maxW = std::max(0, bounds_.w - 4);
            screen.putText(bounds_.x + 1, bounds_.y, text::clip(label, maxW), line, t.windowBg, StyleBold);
        }

        relayoutContent();
        if (content_) {
            // Focus propagated only to the child (selection handled by the container below).
            content_->setFocused(focused_);
            content_->draw(screen);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (content_) return content_->handleKey(ev);
        return KeyEventResult::Ignored;
    }

    bool activate() override {
        if (content_) return content_->activate();
        return false;
    }

    bool selectable() const override {
        return content_ ? content_->selectable() : false;
    }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        return content_ ? content_->cursorPosition() : std::nullopt;
    }

private:
    void relayoutContent() {
        if (!content_ || bounds_.w < 3 || bounds_.h < 3) return;
        content_->setBounds(Rect{bounds_.x + 1, bounds_.y + 1, bounds_.w - 2, bounds_.h - 2});
    }

    std::u32string title_;
    std::unique_ptr<Widget> content_;
};

} // namespace fenrir
