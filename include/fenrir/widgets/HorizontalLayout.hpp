#pragma once

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// One-row layout for reusable toolbars and compact control groups. Children
// have fixed widths or share the remaining width by flex weight.
class HorizontalLayout : public Widget {
public:
    int addFixed(std::unique_ptr<Widget> child, int width) {
        return add(std::move(child), std::max(1, width), 0);
    }

    int addFlexible(std::unique_ptr<Widget> child, int weight = 1) {
        return add(std::move(child), 0, std::max(1, weight));
    }

    Widget* child(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < slots_.size()
                   ? slots_[static_cast<std::size_t>(index)].widget.get()
                   : nullptr;
    }

    int focusedIndex() const { return focused_; }
    void focus(int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < slots_.size()) focused_ = index;
    }

    bool editable() const override {
        const Widget* selected = child(focused_);
        return selected && selected->editable();
    }

    bool selectable() const override {
        return std::any_of(slots_.begin(), slots_.end(),
                           [](const Slot& slot) { return slot.widget->selectable(); });
    }

    int minWidth() const override {
        int fixed = 0;
        int flexible = 0;
        for (const auto& slot : slots_) {
            if (slot.weight == 0) fixed += slot.fixedWidth;
            else flexible += std::max(1, slot.widget->minWidth());
        }
        return fixed + flexible;
    }

    int minHeight() const override {
        int height = 0;
        for (const auto& slot : slots_) height = std::max(height, slot.widget->minHeight());
        return height;
    }

    void draw(Screen& screen) override {
        relayout();
        ensureFocus();
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
            auto& item = *slots_[static_cast<std::size_t>(i)].widget;
            const bool active = (focused_ || highlighted_) && i == focused_;
            item.setFocused(focused_ && active);
            item.setHighlighted(active);
            item.draw(screen);
        }
    }

    KeyEventResult handleKey(const KeyEvent& event) override {
        ensureFocus();
        if (focused_ < 0) return KeyEventResult::Ignored;
        if (event.key == Key::Tab && !event.ctrl && !event.alt) {
            moveFocus(1);
            return KeyEventResult::Consumed;
        }

        Widget* selected = child(focused_);
        if (!selected) return KeyEventResult::Ignored;
        if (!selected->editable() && (event.key == Key::Left || event.key == Key::Right)) {
            moveFocus(event.key == Key::Right ? 1 : -1);
            return KeyEventResult::Consumed;
        }
        if ((event.key == Key::Enter ||
             (event.key == Key::Char && !event.ctrl && event.character == U' ')) &&
            selected->activate())
            return KeyEventResult::Activated;
        return selected->handleKey(event);
    }

    bool activate() override {
        ensureFocus();
        Widget* selected = child(focused_);
        return selected && selected->activate();
    }

    bool clickAt(int x, int y) override {
        relayout();
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
            Widget* item = child(i);
            if (!item || !item->bounds().contains(x, y)) continue;
            focused_ = i;
            if (item->clickAt(x, y)) return true;
            if (item->activate()) return true;
            return item->selectable();
        }
        return false;
    }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        const Widget* selected = child(focused_);
        return selected ? selected->cursorPosition() : std::nullopt;
    }

private:
    struct Slot {
        std::unique_ptr<Widget> widget;
        int fixedWidth = 0;
        int weight = 0;
    };

    int add(std::unique_ptr<Widget> child, int fixedWidth, int weight) {
        slots_.push_back(Slot{std::move(child), fixedWidth, weight});
        if (focused_ < 0 && slots_.back().widget->selectable())
            focused_ = static_cast<int>(slots_.size()) - 1;
        return static_cast<int>(slots_.size()) - 1;
    }

    void ensureFocus() {
        if (child(focused_) && child(focused_)->selectable()) return;
        focused_ = -1;
        for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
            if (child(i)->selectable()) { focused_ = i; return; }
        }
    }

    void moveFocus(int direction) {
        if (slots_.empty()) return;
        int index = focused_ < 0 ? 0 : focused_;
        for (std::size_t probe = 0; probe < slots_.size(); ++probe) {
            index = (index + direction + static_cast<int>(slots_.size())) %
                    static_cast<int>(slots_.size());
            if (child(index)->selectable()) { focused_ = index; return; }
        }
    }

    void relayout() {
        int fixedTotal = 0;
        int totalWeight = 0;
        for (const auto& slot : slots_) {
            fixedTotal += slot.fixedWidth;
            totalWeight += slot.weight;
        }
        int remaining = std::max(0, bounds_.w - fixedTotal);
        int weightsLeft = totalWeight;
        int x = bounds_.x;
        for (auto& slot : slots_) {
            int width = slot.fixedWidth;
            if (slot.weight > 0 && weightsLeft > 0) {
                width = remaining * slot.weight / weightsLeft;
                remaining -= width;
                weightsLeft -= slot.weight;
            }
            width = std::clamp(width, 0, std::max(0, bounds_.x + bounds_.w - x));
            slot.widget->setBounds(Rect{x, bounds_.y, width, bounds_.h});
            x += width;
        }
    }

    std::vector<Slot> slots_;
    int focused_ = -1;
};

} // namespace fenrir
