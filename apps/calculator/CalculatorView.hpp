#pragma once
#include <algorithm>
#include <memory>
#include <string>

#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Button.hpp"
#include "fenrir/widgets/Label.hpp"
#include "fenrir/widgets/Panel.hpp"

#include "CalculatorEngine.hpp"

namespace fenrir::apps {

// Widok kalkulatora. Uzywa TYLKO frameworkowych widgetow:
//  - Label na display (kolory z motywu: pole tekstowe)
//  - Panel jako kontener 20 Buttonow (Panel NAWIGUJE i HIGHLIGHT-uje dzieci)
//
// Panel obsluguje: strzalki nawigacji, Enter/Space aktywacja, podswietlanie.
// CalculatorView tylko: obsluga cyfr/operatorow z klawiatury (bezposrednio na silnik).
// Ctrl+C = Clear jest zarejestrowany jako SKROT w CalculatorApp (Desktop
// wywoluje skróty przed widgetem — handleKey nie powtarza tej obslugi).

class CalculatorView : public Widget {
public:
    CalculatorView() : panel_(4, 1, 0) {
        const auto& t = theme::active;
        display_ = std::make_unique<Label>(U"0", t.fieldText, t.fieldBg);
        display_->setAlignment(LabelAlign::Right);
        buildButtons();
    }

    void draw(Screen& screen) override {
        Rect displayRect{bounds_.x, bounds_.y, bounds_.w, std::max(0, bounds_.h / 4)};
        display_->setBounds(displayRect);
        display_->draw(screen);

        Rect panelRect{bounds_.x, displayRect.y + displayRect.h, bounds_.w,
                       std::max(0, bounds_.h - displayRect.h)};
        panel_.setBounds(panelRect);
        panel_.draw(screen); // Panel sam obsluguje relayout + highlight dzieci
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Direct char input -> silnik licznikowy (nie nawigacja przyciskow).
        if (ev.key == Key::Char && !ev.ctrl) {
            if (handleDirectChar(ev.character)) return KeyEventResult::Consumed;
        }

        // Pozostale klawisze -> panel obsluguje nawigacje (strzalki, Enter).
        return panel_.handleKey(ev);
    }

    // resetEngine() wywolywana z menu aplikacji (setMenuItems) — musi byc publiczna.
    void resetEngine() { engine_.clear(); refreshDisplay(); }

private:
    CalculatorEngine engine_;
    std::unique_ptr<Label> display_;
    Panel panel_;

    bool handleDirectChar(char32_t ch) {
        if (ch >= U'0' && ch <= U'9') { engine_.inputDigit(static_cast<char>(ch)); }
        else if (ch == U'.') { engine_.inputDot(); }
        else if (ch == U'+') { engine_.setOp('+'); }
        else if (ch == U'-') { engine_.setOp('-'); }
        else if (ch == U'*') { engine_.setOp('*'); }
        else if (ch == U'/') { engine_.setOp('/'); }
        else if (ch == U'%') { engine_.percent(); }
        else if (ch == U'=') { engine_.equals(); }
        else return false;

        refreshDisplay();
        return true;
    }

    void refreshDisplay() {
        std::string s = engine_.display();
        display_->setText(std::u32string(s.begin(), s.end()));
    }

    void buildButtons() {
        const std::u32string spec[20] = {
            U"C",   U"+/-", U"%",  U"/",
            U"7",   U"8",   U"9",  U"*",
            U"4",   U"5",   U"6",  U"-",
            U"1",   U"2",   U"3",  U"+",
            U"0",   U".",   U"x^2", U"=",
        };

        // Naturalna siatka 4-kolumnowa — domyślne span=1 dla każdego przycisku.
        // Bez wymuszania szerokich spanów, które łamią ograniczenia Panel (np. "0"
        // na 2 kolumny = wiersz 5 ma 5 slotów > 4 → "=" odpada do wiersza 6).

        for (int i = 0; i < 20; ++i) {
            auto b = std::make_unique<Button>(spec[i]);
            auto* raw = b.get();
            raw->onActivate = [this, label = std::u32string(spec[i])]() {
                dispatch(label);
                refreshDisplay();
            };
            panel_.addChild(std::move(b));
        }
    }

    void dispatch(const std::u32string& label) {
        if (label == U"C")    engine_.clear();
        else if (label == U"+/-") engine_.negate();
        else if (label == U"%")   engine_.percent();
        else if (label == U"/")   engine_.setOp('/');
        else if (label == U"*")   engine_.setOp('*');
        else if (label == U"-")   engine_.setOp('-');
        else if (label == U"+")   engine_.setOp('+');
        else if (label == U"=")   engine_.equals();
        else if (label == U"x^2") engine_.square();
        else if (label == U".")   engine_.inputDot();
        else if (label.size() == 1) engine_.inputDigit(static_cast<char>(label[0]));
    }
};

} // namespace fenrir::apps
