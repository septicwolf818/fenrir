#pragma once
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "CalculatorView.hpp"

namespace fenrir::apps {

class CalculatorApp : public App {
public:
    std::string name() const override { return "Kalkulator"; }

    std::unique_ptr<Widget> createView(AppContext&) override {
        auto view = std::make_unique<CalculatorView>();
        CalculatorView* raw = view.get();

        // Menu Plik: Clear display, Reset to zero
        MenuItem clearItem{U"Czyść ekran", [raw]() { raw->resetEngine(); }};
        MenuItem resetItem{U"Resetuj do zera", [raw]() { raw->resetEngine(); }};

        // Menu Pomoc: About
        MenuItem aboutItem{U"O kalkulatorze v1.0", []() {}};

        raw->setMenuTabs({
            {U"Plik", std::vector<MenuItem>{std::move(clearItem), std::move(resetItem)}},
            {U"Pomoc", std::vector<MenuItem>{std::move(aboutItem)}}
        });

        // Ctrl+C = Clear, Ctrl+R = Reset
        KeyShortcut scClear{};
        scClear.ctrl = true;
        scClear.key = Key::Char;
        scClear.character = static_cast<char32_t>(U'C');
        raw->pushShortcut(scClear, [raw]() { raw->resetEngine(); });

        KeyShortcut scReset{};
        scReset.ctrl = true;
        scReset.key = Key::Char;
        scReset.character = static_cast<char32_t>(U'R');
        raw->pushShortcut(scReset, [raw]() { raw->resetEngine(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(32, std::max(20, desktopArea.w - 4));
        int h = std::min(14, std::max(8, desktopArea.h - 4));
        return Rect{desktopArea.x + 5, desktopArea.y + 2, w, h};
    }
};

} // namespace fenrir::apps
