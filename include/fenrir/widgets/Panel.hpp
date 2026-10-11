#pragma once
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Smart container: rozklada dzieci w siatce i NAWIGUJE miedzy nimi.
// Panel sam zarzadza selectedChild_ — strzalki przesuwa zaznaczenie,
// Enter/Space aktywuje wybrany widget przez wirtualny Widget::activate()
// (zadnych dynamic_cast na konkretny typ — kontener nie zna dzieci).
// Apps tylko dodaja dzieci i nie musza recznie obslugiwac nawigacji.

class Panel : public Widget {
public:
    explicit Panel(int cols = 1, int pad = 1, int gap = 0)
        : cols_(std::max(1, cols)), pad_(std::max(0, pad)), gap_(std::max(0, gap)) {}

    void addChild(std::unique_ptr<Widget> w) {
        children_.push_back(std::move(w));
        childSpans_.push_back(1); // domyslnie 1 kolumna
    }

    void addChildSpan(std::unique_ptr<Widget> w, int span) {
        children_.push_back(std::move(w));
        childSpans_.push_back(std::max(1, span));
    }

    const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

    // Aktualny indeks zaznaczonego dziecka (-1 = brak). Panel sam go obsluguje.
    int selectedIndex() const { return selectedChild_; }
    void setSelectedIndex(int idx) { selectedChild_ = idx; }

    void relayout() {
        int n = static_cast<int>(children_.size());
        if (n == 0 || bounds_.w <= 0 || bounds_.h <= 0) return;

        int availW = std::max(0, bounds_.w - 2 * pad_);
        int cellW = std::max(1, (availW - gap_ * (cols_ - 1)) / cols_);

        // Budujemy wiersze dynamicznie: kazdy child zjada "span" slotow w biezacym wierszu.
        int curCol = 0, curRow = 0;
        for (int i = 0; i < n; ++i) {
            int span = static_cast<int>(childSpans_[static_cast<std::size_t>(i)]);
            // Jesli child nie miesci sie w biezacym wierszu -> nowy wiersz.
            if (curCol + span > cols_) {
                curRow++;
                curCol = 0;
            }
            curCol += span;
        }

        int availH = std::max(0, bounds_.h - 2 * pad_);
        int rows = curRow + 1;
        int cellH = std::max(1, (availH - gap_ * (rows - 1)) / rows);

        // Drugi przebieg: bound + zapamietanie (wiersz, kolumna) kazdego dziecka —
        // te indeksy obsluuguja potem nawigacje gore/dol (najblizsza kolumna
        // w sasiednim wierszu), tak tez przy mieszanych spanach.
        childRow_.assign(static_cast<std::size_t>(n), 0);
        childCol_.assign(static_cast<std::size_t>(n), 0);
        curCol = 0;
        curRow = 0;
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
            childRow_[static_cast<std::size_t>(i)] = curRow;
            childCol_[static_cast<std::size_t>(i)] = curCol;
            curCol += span;
        }
    }

    void draw(Screen& screen) override {
        relayout();
        for (std::size_t i = 0; i < children_.size(); ++i) {
            const bool sel = static_cast<int>(i) == selectedChild_;
            children_[i]->setHighlighted(sel);
            // Fokus okna trafia tylko do zaznaczonego dziecka — pozostale
            // wiedza, ze ich okno nie jest aktywne (poprawne stany hot/disable).
            children_[i]->setFocused(sel && focused_);
        }
        for (auto& c : children_) c->draw(screen);
    }

    // Nawigacja wewnatrz Panelu: strzalki przesuwaja selectedChild_ po siatce.
    KeyEventResult handleKey(const KeyEvent& ev) override {
        relayout();
        int n = static_cast<int>(children_.size());
        if (n == 0) return KeyEventResult::Ignored;
        ensureValidSelection(n);

        if (ev.key == Key::Left)  { moveHorizontal(-1, n); return KeyEventResult::Consumed; }
        if (ev.key == Key::Right) { moveHorizontal(+1, n); return KeyEventResult::Consumed; }
        if (ev.key == Key::Up)    { moveVertical(-1, n);   return KeyEventResult::Consumed; }
        if (ev.key == Key::Down)  { moveVertical(+1, n);   return KeyEventResult::Consumed; }

        const bool activationKey =
            ev.key == Key::Enter ||
            (ev.key == Key::Char && !ev.ctrl && ev.character == U' ');
        if (activationKey && selectedChild_ >= 0 &&
            children_[static_cast<std::size_t>(selectedChild_)]->activate()) {
            return KeyEventResult::Activated;
        }

        return delegate(ev, n);
    }

private:
    bool selectableAt(int idx) const {
        return idx >= 0 && static_cast<std::size_t>(idx) < children_.size() &&
               children_[static_cast<std::size_t>(idx)]->selectable();
    }

    void ensureValidSelection(int n) {
        if (selectableAt(selectedChild_)) return;
        selectedChild_ = -1;
        for (int i = 0; i < n; ++i) {
            if (selectableAt(i)) { selectedChild_ = i; return; }
        }
    }

    // Lewo/Prawo: kolejny/wczesniejszy element wzgledem liniowej kolejnosci
    // (z owijaniem), przeskakujacy elementy niezaznaczalne (np. etykiety).
    void moveHorizontal(int dir, int n) {
        int idx = selectedChild_ < 0 ? 0 : selectedChild_;
        for (int step = 0; step < n; ++step) {
            idx = (idx + dir + n) % n;
            if (selectableAt(idx)) { selectedChild_ = idx; return; }
        }
    }

    // Gora/Dol: sasiedni wiersz, kolumna najblizsza biezacej.
    void moveVertical(int dir, int n) {
        if (static_cast<int>(childRow_.size()) != n) { moveHorizontal(dir, n); return; }
        const int curRow = childRow_[static_cast<std::size_t>(selectedChild_)];
        const int curCol = childCol_[static_cast<std::size_t>(selectedChild_)];
        const int maxRow = *std::max_element(childRow_.begin(), childRow_.end());

        for (int probe = 1; probe <= maxRow + 1; ++probe) {
            int targetRow = curRow + dir * probe;
            if (targetRow < 0 || targetRow > maxRow) break;
            int best = -1, bestDist = 0;
            for (int i = 0; i < n; ++i) {
                if (!selectableAt(i)) continue;
                if (childRow_[static_cast<std::size_t>(i)] != targetRow) continue;
                int dist = std::abs(childCol_[static_cast<std::size_t>(i)] - curCol);
                if (best < 0 || dist < bestDist) { bestDist = dist; best = i; }
            }
            if (best >= 0) { selectedChild_ = best; return; }
        }
        // Brak sasiedniego wiersza — nic nie zmieniamy.
    }

    // Przekazuje klawis wybranemu dziecku; Ignored wraca bez zmian (moga go
    // obsluzyc zewnetrzne kontenery / Desktop).
    KeyEventResult delegate(const KeyEvent& ev, int n) {
        if (selectedChild_ >= 0 && static_cast<std::size_t>(selectedChild_) < children_.size())
            return children_[static_cast<std::size_t>(selectedChild_)]->handleKey(ev);
        (void)n;
        return KeyEventResult::Ignored;
    }

    int cols_, pad_, gap_;
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<int> childSpans_;
    std::vector<int> childRow_; // wypelniane przez relayout()
    std::vector<int> childCol_; // wypelniane przez relayout()
    int selectedChild_ = 0;     // pierwszy child jest zaznaczony od startu
};

} // namespace fenrir
