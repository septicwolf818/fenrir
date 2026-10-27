#include "BrowserApp.hpp"

#include <algorithm>

namespace fenrir::apps {

std::unique_ptr<Widget> BrowserApp::createView(AppContext& context) {
    auto view = std::make_unique<browser::BrowserView>(context);
    auto* raw = view.get();
    view->setMenuTabs({
        {U"File", std::vector<MenuItem>{
            {U"New tab (Ctrl+T)", [raw] { raw->newTab(); }},
            {U"Close tab (Ctrl+W)", [raw] { raw->closeTab(); }},
            {U"Exit", [&context] { context.closeWindow(); }},
        }},
        {U"Navigate", std::vector<MenuItem>{
            {U"Back (Ctrl+Left)", [raw] { raw->goBack(); }},
            {U"Forward (Ctrl+Right)", [raw] { raw->goForward(); }},
            {U"Reload (Ctrl+R)", [raw] { raw->reload(); }},
        }},
        {U"Page", std::vector<MenuItem>{
            {U"Open focused link in new tab (Ctrl+Enter)",
             [raw] { raw->openFocusedLinkInNewTab(); }},
        }},
    });
    App::addAboutMenu(*view, context, U"Browser",
                      U"A lightweight HTML/CSS browser rendered with Fenrir's 16-color terminal cells.");
    view->pushShortcut({true, Key::Char, U'l'}, [raw] { (void)raw->handleKey(KeyEvent{Key::Char, U'l', true}); });
    view->pushShortcut({true, Key::Char, U't'}, [raw] { raw->newTab(); });
    view->pushShortcut({true, Key::Char, U'w'}, [raw] { raw->closeTab(); });
    view->pushShortcut({true, Key::Char, U'r'}, [raw] { raw->reload(); });
    return view;
}

Rect BrowserApp::preferredBounds(const Rect& area) const {
    const int width = std::min(82, std::max(32, area.w - 4));
    const int height = std::min(28, std::max(6, area.h - 3));
    return Rect{area.x + std::max(0, (area.w - width) / 2),
                area.y + std::max(0, (area.h - height) / 2), width, height};
}

} // namespace fenrir::apps
