#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// App = abstraction of a program on the desktop: name, view, preferred window.
// Menus and shortcuts come FROM THE CONTENT WIDGET (Widget::menuTabs / shortcuts),
// which is set up by createView() (setMenuTabs/pushShortcut) —
// that's why App has no menuTabs()/shortcuts() of its own.

class App {
public:
    virtual ~App() = default;

    // Name shown in the menu bar / applications menu.
    virtual std::string name() const = 0;

    // Creates a NEW view (state) of the app - called on every launch, so several
    // independent windows of the same app can coexist at once.
    virtual std::unique_ptr<Widget> createView(AppContext& ctx) = 0;

    // Preferred starting size/position of the window relative to the desktop area.
    virtual Rect preferredBounds(const Rect& desktopArea) const {
        int w = std::min(60, std::max(20, desktopArea.w - 4));
        int h = std::min(20, std::max(6, desktopArea.h - 4));
        return Rect{desktopArea.x + 2, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir
