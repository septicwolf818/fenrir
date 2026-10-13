#pragma once
#include <chrono>
#include <optional>
#include <string>
#include <utility>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Status/help bar with left-aligned text and an optional right-aligned label.
// Timed messages temporarily override either side, then the persistent text
// is restored automatically.
class StatusBar : public Widget {
public:
    void setText(std::u32string left, std::u32string right = {}) {
        left_ = std::move(left);
        right_ = std::move(right);
    }

    // Show a temporary message for a short time (default: 2.5 seconds).
    // Pass std::nullopt for a side that should keep its persistent text.
    void setMessage(std::optional<std::u32string> left,
                    std::optional<std::u32string> right = std::nullopt,
                    std::chrono::milliseconds duration = std::chrono::milliseconds(2500)) {
        messageLeft_ = std::move(left);
        messageRight_ = std::move(right);
        messageUntil_ = std::chrono::steady_clock::now() + duration;
        messageActive_ = duration.count() > 0;
    }

    void clearMessage() { messageActive_ = false; }

    const std::u32string& text() const { return left_; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.statusText, t.statusBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        const bool showMessage = messageActive_ && std::chrono::steady_clock::now() < messageUntil_;
        if (messageActive_ && !showMessage) messageActive_ = false;
        const std::u32string& left = showMessage && messageLeft_ ? *messageLeft_ : left_;
        const std::u32string& right = showMessage && messageRight_ ? *messageRight_ : right_;

        // The right label is anchored to the edge, so clip the left text to
        // keep it from overlapping the right column.
        std::u32string r = text::clip(right, bounds_.w);
        int leftW = bounds_.w - (r.empty() ? 0 : static_cast<int>(r.size()) + 1);
        if (leftW < 0) leftW = 0;
        screen.putText(bounds_.x, bounds_.y, text::clip(left, leftW), t.statusText, t.statusBg);

        if (!r.empty()) {
            int rx = bounds_.x + bounds_.w - static_cast<int>(r.size());
            if (rx > bounds_.x) screen.putText(rx, bounds_.y, r, t.statusText, t.statusBg, StyleBold);
        }
    }

    KeyEventResult handleKey(const KeyEvent&) override { return KeyEventResult::Ignored; }

    bool selectable() const override { return false; }

private:
    std::u32string left_;
    std::u32string right_;
    std::optional<std::u32string> messageLeft_;
    std::optional<std::u32string> messageRight_;
    std::chrono::steady_clock::time_point messageUntil_{};
    bool messageActive_ = false;
};

} // namespace fenrir
