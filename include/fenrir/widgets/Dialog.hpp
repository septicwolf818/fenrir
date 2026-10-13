#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Button.hpp"
#include "fenrir/widgets/LineEdit.hpp"
#include "fenrir/widgets/Panel.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Modalny dialog w oknie: komunikat + przyciski (+ pola tekstowe).
// Zastepuje reczne wynalazki w aplikacjach (o "About", potwierdzenie usuniecia,
// prompt przemianowania) — aplikacje dostaja gotowy widget i otwieraja go przez
// AppContext::openDialog(). onResult(idx): idx = numer przycisku, -1 = Esc/anuluj.
//
// Pol tekstowych moze byc wiecej niz jedno (addInput) — np. kalendarz pyta
// o tytul/godziny w jednym okienku. Tab/strzalki przechodza miedzy polami,
// z ostatniego pola na przyciski (i z powrotem); Enter w polu = zatwierdz
// (jakby wcisnac OK).
class Dialog : public Widget {
public:
    enum class Buttons { Ok, OkCancel, YesNo, YesNoCancel };

    explicit Dialog(std::u32string message, Buttons buttons = Buttons::Ok)
        : message_(std::move(message)), buttons_(buttons) {
        buildButtons();
    }

    std::function<void(int index)> onResult;

    void setMessage(std::u32string m) { message_ = std::move(m); }
    const std::u32string& message() const { return message_; }

    // Dodaje pole tekstowe (prompt / formularz). Zwraca wskaznik dla wygody
    // ustawien callbackow. Fokus startuje na pierwszym dodanym polu.
    LineEdit* addInput(std::u32string label = {}, std::u32string initial = {}) {
        inputs_.push_back(std::make_unique<LineEdit>(std::move(initial), std::move(label)));
        LineEdit* raw = inputs_.back().get();
        raw->onSubmit = [this](const std::u32string&) { confirm(0); };
        zone_ = Zone::Input;
        inputIdx_ = 0;
        return raw;
    }
    // Zgodnosc ze starym API (AppContext::prompt): pierwsze pole.
    LineEdit* setInput(std::u32string label = {}, std::u32string initial = {}) {
        return addInput(std::move(label), std::move(initial));
    }
    bool hasInput() const { return !inputs_.empty(); }
    LineEdit* input() { return inputs_.empty() ? nullptr : inputs_.front().get(); }
    LineEdit* input(std::size_t i) { return i < inputs_.size() ? inputs_[i].get() : nullptr; }
    std::size_t inputCount() const { return inputs_.size(); }

    // Wysokosc dialogu dla danej szerokosci — do rozmiarowania okna przez Desktop.
    int heightForWidth(int w) const {
        const int inner = std::max(1, w - 2);
        const int msgLines = static_cast<int>(text::wrap(message_, inner).size());
        const int ins = static_cast<int>(inputs_.size());
        return 1 + msgLines + ins + 1 + 1 + 1; // pad + tekst + pola + odstep + przyciski + pad
    }

    bool editable() const override { return hasInput(); }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        const int inner = std::max(1, bounds_.w - 2);
        int y = bounds_.y + 1;
        for (const auto& line : text::wrap(message_, inner)) {
            if (y >= bounds_.y + bounds_.h - 2) break;
            screen.putText(bounds_.x + 1, y, line, t.surfaceText, t.surfaceBg);
            ++y;
        }

        for (std::size_t i = 0; i < inputs_.size(); ++i) {
            if (y >= bounds_.y + bounds_.h - 2) break;
            auto& in = inputs_[i];
            in->setBounds(Rect{bounds_.x + 1, y, std::max(1, bounds_.w - 2), 1});
            in->setFocused(focused_ && zone_ == Zone::Input && i == inputIdx_);
            in->draw(screen);
            ++y;
        }

        // Przyciski na ostatnim wierszu (Panel rozklada je rownomiernie).
        panel_->setBounds(Rect{bounds_.x, bounds_.y + bounds_.h - 1, bounds_.w, 1});
        panel_->setFocused(focused_ && zone_ == Zone::Buttons);
        panel_->draw(screen);
    }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        if (zone_ == Zone::Input && inputIdx_ < inputs_.size())
            return inputs_[inputIdx_]->cursorPosition();
        return std::nullopt;
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Esc = anuluj (zawsze, niezaleznie od strefy).
        if (ev.key == Key::Escape) {
            fire(-1);
            return KeyEventResult::Activated;
        }
        // Tab = przelaczanie stref: pole -> kolejne pole -> przyciski -> pole...
        if (ev.key == Key::Tab && hasInput()) {
            if (zone_ == Zone::Buttons) {
                zone_ = Zone::Input;
                inputIdx_ = 0;
            } else if (inputIdx_ + 1 < inputs_.size()) {
                ++inputIdx_;
            } else {
                zone_ = Zone::Buttons;
            }
            return zone_ == Zone::Input ? KeyEventResult::EnteredEdit : KeyEventResult::Consumed;
        }

        if (zone_ == Zone::Input) {
            // Up/Down = miedzy polami (z krajow na przyciski i z powrotem).
            if (ev.key == Key::Up) {
                if (inputIdx_ > 0) --inputIdx_;
                else zone_ = Zone::Buttons;
                return KeyEventResult::Consumed;
            }
            if (ev.key == Key::Down) {
                if (inputIdx_ + 1 < inputs_.size()) ++inputIdx_;
                else zone_ = Zone::Buttons;
                return KeyEventResult::Consumed;
            }
            KeyEventResult r = inputs_[inputIdx_]->handleKey(ev);
            return r == KeyEventResult::Ignored ? KeyEventResult::Consumed : r;
        }

        // Strefa przyciskow: strzalka gore na pierwszym przycisku wraca do
        // ostatniego pola (dla jednego pola — do niego, jak dawniej).
        if (hasInput() && ev.key == Key::Up && panel_->selectedIndex() == 0) {
            zone_ = Zone::Input;
            inputIdx_ = inputs_.size() - 1;
            return KeyEventResult::Consumed;
        }
        return panel_->handleKey(ev);
    }

    bool activate() override {
        if (zone_ == Zone::Input) { confirm(0); return true; }
        return panel_->activate();
    }

    bool selectable() const override { return true; }

private:
    enum class Zone { Input, Buttons };

    static std::vector<std::u32string> labelsFor(Buttons b) {
        switch (b) {
            case Buttons::Ok:            return {U"OK"};
            case Buttons::OkCancel:      return {U"OK", U"Anuluj"};
            case Buttons::YesNo:         return {U"Tak", U"Nie"};
            case Buttons::YesNoCancel:   return {U"Tak", U"Nie", U"Anuluj"};
        }
        return {U"OK"};
    }

    void buildButtons() {
        auto labels = labelsFor(buttons_);
        panel_ = std::make_unique<Panel>(static_cast<int>(labels.size()), 0, 1);
        for (std::size_t i = 0; i < labels.size(); ++i) {
            auto btn = std::make_unique<Button>(labels[i]);
            const int idx = static_cast<int>(i);
            btn->onActivate = [this, idx] { confirm(idx); };
            panel_->addChild(std::move(btn));
        }
        zone_ = Zone::Buttons;
    }

    void confirm(int idx) { fire(idx); }

    void fire(int idx) {
        if (onResult) {
            auto cb = onResult; // kopiujemy — callback moze zamknac to okno (Dialog zniszczony)
            onResult = {};
            cb(idx);
        }
    }

    std::u32string message_;
    Buttons buttons_;
    std::vector<std::unique_ptr<LineEdit>> inputs_;
    std::size_t inputIdx_ = 0;
    std::unique_ptr<Panel> panel_;
    Zone zone_ = Zone::Buttons;
};

} // namespace fenrir
