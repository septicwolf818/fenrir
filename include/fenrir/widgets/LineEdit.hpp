#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <string_view>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Single-line text field with a cursor and horizontal scrolling.
// Works as window content (then editing -> Edit mode with the GUI cursor)
// or as a dialog child (prompt / file name field).
class LineEdit : public Widget {
public:
    explicit LineEdit(std::u32string initial = {}, std::u32string label = {})
        : value_(std::move(initial)), label_(std::move(label)) {
        editable_ = true; // a text field always wants a cursor
        // Cursor at the END of the initial value (as in setValue) — without this
        // the save dialog started with the cursor at 0 and Backspace deleted nothing.
        cursor_ = static_cast<int>(value_.size());
    }

    std::function<void(const std::u32string&)> onSubmit; // Enter
    std::function<void(const std::u32string&)> onChange;  // every content change

    const std::u32string& value() const { return value_; }
    void setValue(std::u32string v) {
        value_ = std::move(v);
        cursor_ = static_cast<int>(value_.size());
        scrollX_ = 0;
        if (onChange) onChange(value_);
    }
    void setLabel(std::u32string l) { label_ = std::move(l); }
    void setReadOnly(bool r) { readOnly_ = r; }
    int cursor() const { return cursor_; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.fieldText, t.fieldBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        // Label (e.g. "Name: ") at the start of the field.
        int prefixW = 0;
        if (!label_.empty()) {
            std::u32string lab = text::clip(label_, bounds_.w);
            prefixW = static_cast<int>(lab.size());
            screen.putText(bounds_.x, bounds_.y, lab, t.fieldText, t.fieldBg, StyleBold);
        }

        const int fieldW = bounds_.w - prefixW;
        if (fieldW <= 0) return;
        ensureVisible(fieldW);

        // Visible slice of the value (scrolled so the cursor stays in view).
        const int start = std::clamp(scrollX_, 0, std::max(0, static_cast<int>(value_.size())));
        const int avail = std::min(fieldW, static_cast<int>(value_.size()) - start);
        if (avail > 0) {
            screen.putText(bounds_.x + prefixW, bounds_.y,
                           std::u32string_view(value_).substr(static_cast<std::size_t>(start),
                                                              static_cast<std::size_t>(avail)),
                           t.fieldText, t.fieldBg);
        }
        // Underline for the active field — still clear when the GUI cursor is not visible.
        if (focused_) {
            for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x) {
                Cell c = screen.at(x, bounds_.y);
                c.style |= StyleUnderline;
                screen.set(x, bounds_.y, c);
            }
        }
    }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        if (bounds_.w <= 0 || bounds_.h <= 0) return std::nullopt;
        const int prefixW = static_cast<int>(label_.size());
        const int fieldW = bounds_.w - prefixW;
        if (fieldW <= 0) return std::nullopt;
        const int col = prefixW + (cursor_ - scrollX_);
        if (col < 0 || col >= bounds_.w) return std::nullopt;
        return std::make_pair(bounds_.x + col, bounds_.y);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (readOnly_) return KeyEventResult::Ignored;
        switch (ev.key) {
            case Key::Left:
                if (cursor_ > 0) --cursor_;
                return KeyEventResult::Consumed;
            case Key::Right:
                if (cursor_ < static_cast<int>(value_.size())) ++cursor_;
                return KeyEventResult::Consumed;
            case Key::Home:
                cursor_ = 0;
                return KeyEventResult::Consumed;
            case Key::End:
                cursor_ = static_cast<int>(value_.size());
                return KeyEventResult::Consumed;
            case Key::Backspace:
                if (cursor_ > 0) {
                    value_.erase(value_.begin() + cursor_ - 1);
                    --cursor_;
                    if (onChange) onChange(value_);
                }
                return KeyEventResult::Consumed;
            case Key::Delete:
                if (cursor_ < static_cast<int>(value_.size())) {
                    value_.erase(value_.begin() + cursor_);
                    if (onChange) onChange(value_);
                }
                return KeyEventResult::Consumed;
            case Key::Enter:
                if (onSubmit) onSubmit(value_);
                return KeyEventResult::Consumed;
            case Key::Char:
                if (!ev.ctrl && !ev.alt) {
                    value_.insert(value_.begin() + cursor_, ev.character);
                    ++cursor_;
                    if (onChange) onChange(value_);
                    // We want to show the cursor — ask Desktop for Edit mode.
                    return KeyEventResult::EnteredEdit;
                }
                return KeyEventResult::Ignored;
            default:
                return KeyEventResult::Ignored;
        }
    }

    bool activate() override {
        if (onSubmit) onSubmit(value_);
        return true;
    }

    bool selectable() const override { return enabled_ && !readOnly_; }

private:
    void ensureVisible(int fieldW) {
        if (cursor_ < scrollX_) scrollX_ = cursor_;
        if (cursor_ >= scrollX_ + fieldW) scrollX_ = cursor_ - fieldW + 1;
        scrollX_ = std::max(0, scrollX_);
    }

    std::u32string value_;
    std::u32string label_;
    int cursor_ = 0;
    int scrollX_ = 0;
    bool readOnly_ = false;
};

} // namespace fenrir
