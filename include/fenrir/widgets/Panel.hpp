#pragma once
#include <algorithm>
#include <memory>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Smart container: rozkłada dzieci w siatce i NAWIGUJE między nimi.
// Panel sam zarządza selectedChild_ — strzałki przesuwa zaznaczenie,
// Enter/Space aktywuje wybrany widget. Apps tylko dodają dzieci i nie
// muszą ręcznie obsługiwać nawigacji klawiatury.

class Panel : public Widget {
public:
    explicit Panel(int cols = 1, int pad = 1, int gap = 0)
        : cols_(std::max(1, cols)), pad_(std::max(0, pad)), gap_(std::max(0, gap)) {}

    void addChild(std::unique_ptr<Widget> w) {
        children_.push_back(std::move(w));
        childSpans_.push_back(1); // domyślnie 1 kolumna
    }

    void addChildSpan(std::unique_ptr<Widget> w, int span) {
        children_.push_back(std::move(w));
        childSpans_.push_back(std::max(1, span));
    }

    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

    // Aktualny indeks zaznaczonego dziecka (-1 = brak). Panel sam go obsługuje.
    int selectedIndex() const { return selectedChild_; }
    void setSelectedIndex(int idx) { selectedChild_ = idx; }

    void relayout() {
        int n = static_cast<int>(children_.size());
        if (n == 0 || bounds_.w <= 0 || bounds_.h <= 0) return;

        int availW = std::max(0, bounds_.w - 2 * pad_);
        int cellW = std::max(1, (availW - gap_ * (cols_ - 1)) / cols_);

        // Budujemy wiersze dynamicznie: każdy child zjada "span" slotów w bieżącym wierszu.
        int curCol = 0, curRow = 0;
        for (int i = 0; i < n; ++i) {
            int span = static_cast<int>(childSpans_[static_cast<std::size_t>(i)]);
            // Jeśli child nie mieści się w bieżącym wierszu -> nowy wiersz.
            if (curCol + span > cols_) {
                curRow++;
                curCol = 0;
            }
            curCol += span;
        }

        int availH = std::max(0, bounds_.h - 2 * pad_);
        int rows = curRow + 1;
        int cellH = std::max(1, (availH - gap_ * (rows - 1)) / rows);

        // Przechodzimy ponownie i ustawiamy bound dla każdego childa.
        curCol = 0; curRow = 0;
        for (int i = 0; i < n; ++i) {
            int span = static_cast<int>(childSpans_[static_cast<std::size_t>(i)]);
            if (curCol + span > cols_) {
                curRow++;
                curCol = 0;
            }
            int x = bounds_.x + pad_ + curCol * (cellW + gap_);
            int y = bounds_.y + pad_ + curRow * (cellH + gap_);
            int w = span * cellW + (span - 1) * gap_;
            children_[static_cast<std::size_t>(i)]->setBounds(Rect{x, y, std::max(0, w), std::max(0, cellH)});
            curCol += span;
        }
    }

    void draw(Screen& screen) override {
        relayout();
        for (std::size_t i = 0; i < children_.size(); ++i) {
            auto* b = dynamic_cast<Button*>(children_[i].get());
            if (b) b->setHighlighted(static_cast<int>(i) == selectedChild_);
            else children_[static_cast<std::size_t>(i)]->setHighlighted(false);
        }
        for (auto& c : children_) c->draw(screen);
    }

    // Nawigacja wewnątrz Panelu: strzałki przesuwają selectedChild_ w siatce.
    KeyEventResult handleKey(const KeyEvent& ev) override {
        int n = static_cast<int>(children_.size());
        if (n == 0) return KeyEventResult::Ignored;

        // Strzałki: nawigacja w siatce kolumnowej.
        // Lewo/Prawo -> +/-1 (kolumna); Gora/Dol -> +/-cols_ (wiersz).
        if (ev.key == Key::Left) {
            --selectedChild_;
            if (selectedChild_ < 0) selectedChild_ = n - 1;
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Right) {
            ++selectedChild_;
            if (selectedChild_ >= n) selectedChild_ = 0;
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Up) {
            selectedChild_ -= cols_;
            if (selectedChild_ < 0) selectedChild_ = 0; // clamp to first row
            return KeyEventResult::Consumed;
        }
        if (ev.key == Key::Down) {
            selectedChild_ += cols_;
            if (static_cast<std::size_t>(selectedChild_) >= children_.size())
                selectedChild_ = n - 1; // clamp to last element
            return KeyEventResult::Consumed;
        }

        // Enter/Space: aktywuj wybrany widget.
        if (ev.key == Key::Enter || (ev.key == Key::Char && !ev.ctrl && ev.character == U' ')) {
            if (selectedChild_ >= 0 && static_cast<std::size_t>(selectedChild_) < children_.size()) {
                auto* b = dynamic_cast<Button*>(children_[static_cast<std::size_t>(selectedChild_)].get());
                if (b && b->onActivate) {
                    b->onActivate();
                    return KeyEventResult::Activated;
                }
            }
            // Jeśli to nie Button, delegujemy do child's handleKey.
            auto* content = children_[static_cast<std::size_t>(selectedChild_)].get();
            if (content) {
                auto result = content->handleKey(ev);
                return result != KeyEventResult::Ignored ? result : KeyEventResult::Consumed;
            }
        }

        // Delegate non-navigation keys to the selected child.
        if (selectedChild_ >= 0 && static_cast<std::size_t>(selectedChild_) < children_.size()) {
            auto* content = children_[static_cast<std::size_t>(selectedChild_)].get();
            if (content) {
                auto result = content->handleKey(ev);
                return result != KeyEventResult::Ignored ? result : KeyEventResult::Consumed;
            }
        }

        return KeyEventResult::Ignored;
    }

private:
    int cols_, pad_, gap_;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<int> childSpans_;
    int selectedChild_ = 0; // pierwszy child jest zaznaczony od startu
};

} // namespace fenrir
