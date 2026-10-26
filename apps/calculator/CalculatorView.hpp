#pragma once
#include <algorithm>
#include <memory>
#include <string>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Button.hpp"
#include "fenrir/widgets/Label.hpp"
#include "fenrir/widgets/Panel.hpp"
#include "fenrir/app/AppContext.hpp"

#include "CalculatorEngine.hpp"

namespace fenrir::apps {

// Calculator view. Uses ONLY framework widgets:
//  - Label for the display (colors from the theme: text field)
//  - Panel as the container for the 20 buttons (Panel NAVIGATES and HIGHLIGHTS the children)
//
// Panel handles: arrow-key navigation, Enter/Space activation, highlighting.
// CalculatorView only: keyboard digit/operator handling (straight onto the engine).
// Ctrl+C = Clear is registered as a SHORTCUT in CalculatorApp (Desktop runs the
// shortcuts before the widget — handleKey does not repeat that handling).

class CalculatorView : public Widget {
public:
    explicit CalculatorView(AppContext& ctx) : ctx_(ctx), panel_(4, 1, 0) {
        const auto& t = theme::active;
        display_ = std::make_unique<Label>(U"0", t.fieldText, t.fieldBg);
        display_->setAlignment(LabelAlign::Right);
        buildButtons();
    }

    void draw(Screen& screen) override {
        ctx_.setWindowTitle(std::string("Calculator: ") + engine_.display());
        // Colors from the current theme — theme::apply swaps active on the fly.
        display_->setColors(theme::active.fieldText, theme::active.fieldBg);
        Rect displayRect{bounds_.x, bounds_.y, bounds_.w, std::max(0, bounds_.h / 4)};
        display_->setBounds(displayRect);
        display_->draw(screen);

        Rect panelRect{bounds_.x, displayRect.y + displayRect.h, bounds_.w,
                       std::max(0, bounds_.h - displayRect.h)};
        panel_.setBounds(panelRect);
        panel_.draw(screen); // Panel handles relayout + child highlighting itself
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Direct char input -> the engine (no button navigation).
        if (ev.key == Key::Char && !ev.ctrl && !ev.alt) {
            if (handleDirectChar(ev.character)) return KeyEventResult::Consumed;
        }

        // Remaining keys -> the panel handles navigation (arrows, Enter).
        return panel_.handleKey(ev);
    }

    // resetEngine() is called from the app menu (setMenuTabs) — must be public.
    void resetEngine() { engine_.clear(); refreshDisplay(); }

    // Minimum: each column fits the widest label, "[ x^2 ]" (7 cells),
    // plus the panel's two padding cells.
    int minWidth() const override { return 30; }
    int minHeight() const override { return 8; }

private:
    CalculatorEngine engine_;
    AppContext& ctx_;
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
        display_->setText(text::toUtf32(s));
    }

    void buildButtons() {
        const std::u32string spec[20] = {
            U"C",   U"+/-", U"%",  U"/",
            U"7",   U"8",   U"9",  U"*",
            U"4",   U"5",   U"6",  U"-",
            U"1",   U"2",   U"3",  U"+",
            U"0",   U".",   U"x^2", U"=",
        };

        // Natural 4-column grid — default span=1 for every button.
        // Without forcing wide spans, which break Panel constraints (e.g. "0"
        // across 2 columns = row 5 has 5 slots > 4 → "=" drops to row 6).

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
