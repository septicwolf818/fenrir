#pragma once
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "CalculatorView.hpp"

namespace fenrir::apps {

class CalculatorApp : public App {
public:
    std::string name() const override { return "Calculator"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<CalculatorView>();
        CalculatorView* raw = view.get();

        // File menu: clear the calculator (same action as Ctrl+C).
        MenuItem clearItem{U"Clear (Ctrl+C)", [raw]() { raw->resetEngine(); }};

        raw->setMenuTabs({
            {U"File", std::vector<MenuItem>{std::move(clearItem)}}
        });
        addExitMenu(*view, ctx);
        addAboutMenu(*view, ctx, U"Calculator",
                     U"A keyboard-driven four-function calculator. Type numbers and operators, "
                     U"or navigate buttons with arrows and activate them with Enter/Space.");

        // Ctrl+C = Clear (shortcut registered on the widget — Desktop runs the
        // shortcuts before handleKey, so handleKey doesn't deal with it).
        raw->pushShortcut({true, Key::Char, U'c'}, [raw]() { raw->resetEngine(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        // Keep the preferred inset when possible, but slide toward the edge
        // on narrow areas. Minimums 32x11 include the frame and menu strip.
        int w = 32;
        int h = std::min(14, std::max(11, desktopArea.h - 4));
        int x = desktopArea.x + std::min(5, std::max(0, desktopArea.w - w));
        int y = desktopArea.y + std::min(2, std::max(0, desktopArea.h - h));
        return Rect{x, y, w, h};
    }
};

} // namespace fenrir::apps
