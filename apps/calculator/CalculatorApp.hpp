#pragma once
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "CalculatorView.hpp"

namespace fenrir::apps {

class CalculatorApp : public App {
public:
    std::string name() const override { return "Kalkulator"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<CalculatorView>();
        CalculatorView* raw = view.get();

        // Menu Plik: Clear display, Reset to zero
        MenuItem clearItem{U"Czysc ekran (Ctrl+C)", [raw]() { raw->resetEngine(); }};
        MenuItem resetItem{U"Resetuj do zera", [raw]() { raw->resetEngine(); }};

        // Menu Pomoc: About — dialog z biblioteki (AppContext::showMessage).
        MenuItem aboutItem{U"O kalkulatorze", [&ctx]() {
            ctx.showMessage(U"Fenrir Kalkulator v1.0\n\nCyfry i operatory z klawiatury,\n"
                            U"Enter/strzalki nawiguja po przyciskach.");
        }};

        raw->setMenuTabs({
            {U"Plik", std::vector<MenuItem>{std::move(clearItem), std::move(resetItem)}},
            {U"Pomoc", std::vector<MenuItem>{std::move(aboutItem)}}
        });

        // Ctrl+C = Clear (skrot rejestrowany na widgetcie — Desktop wywoluje
        // skróty przed handleKey, wiec handleKey sie tym nie zajmuje).
        raw->pushShortcut({true, Key::Char, U'c'}, [raw]() { raw->resetEngine(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(32, std::max(20, desktopArea.w - 4));
        int h = std::min(14, std::max(8, desktopArea.h - 4));
        return Rect{desktopArea.x + 5, desktopArea.y + 2, w, h};
    }
};

} // namespace fenrir::apps
