#include "fenrir/window/Window.hpp"

#include <algorithm>

#include "fenrir/core/Theme.hpp"

namespace fenrir {

Window::Window(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool hasBorder)
    : title_(std::move(title)), bounds_(bounds), content_(std::move(content)), hasBorder_(hasBorder) {
    // A window cannot be created smaller than what the content needs (app
    // preferredBounds are often clipped on small terminals).
    bounds_.w = std::max(bounds_.w, minWindowWidth());
    bounds_.h = std::max(bounds_.h, minWindowHeight());
    relayoutContent();
}

void Window::moveTo(int x, int y) {
    bounds_.x = x;
    bounds_.y = y;
    relayoutContent();
}

void Window::moveBy(int dx, int dy) {
    bounds_.x += dx;
    bounds_.y += dy;
    relayoutContent();
}

void Window::resizeBy(int dw, int dh) {
    // During resize (F6) the content-derived minimum also applies —
    // the window cannot be shrunk below the widget's minWidth/minHeight.
    bounds_.w = std::max(minWindowWidth(), bounds_.w + dw);
    bounds_.h = std::max(minWindowHeight(), bounds_.h + dh);
    relayoutContent();
}

void Window::constrainTo(const Rect& area) {
    const int maxW = std::max(1, area.w);
    const int maxH = std::max(1, area.h);
    bounds_.w = std::clamp(bounds_.w, 1, maxW);
    bounds_.h = std::clamp(bounds_.h, 1, maxH);
    bounds_.x = std::clamp(bounds_.x, area.x, area.x + maxW - bounds_.w);
    bounds_.y = std::clamp(bounds_.y, area.y, area.y + maxH - bounds_.h);
    relayoutContent();
}

int Window::minWindowWidth() const {
    const int contentMin = content_ ? content_->minWidth() : 0;
    return std::max(kMinWidth, contentMin + (hasBorder_ ? 2 : 0));
}

int Window::minWindowHeight() const {
    // relayoutContent(): contentH = h - 1 - (1 + menu) with a frame,
    // i.e. h = contentH + 2 + (menu ? 1 : 0); without a frame h = contentH.
    const int chrome = hasBorder_ ? 2 + (hasMenuTabs() ? 1 : 0) : 0;
    const int contentMin = content_ ? content_->minHeight() : 0;
    return std::max(kMinHeight, contentMin + chrome);
}

void Window::setFocused(bool f) {
    focused_ = f;
    if (content_) content_->setFocused(f);
}

void Window::maximize(const Rect& area) {
    if (!maximized_) {
        restoreBounds_ = bounds_;
        maximized_ = true;
    }
    // On macro-terminals area can be smaller than the content minimum — as in
    // resizeBy, we clamp to minWindow* so the content is not cut off.
    bounds_ = Rect{area.x, area.y,
                   std::max(area.w, minWindowWidth()),
                   std::max(area.h, minWindowHeight())};
    relayoutContent();
}

void Window::restore() {
    if (!maximized_) return;
    bounds_ = restoreBounds_;
    maximized_ = false;
    relayoutContent();
}

bool Window::onTitleBar(int x, int y) const {
    return hasBorder_ && y == bounds_.y && x >= bounds_.x && x < bounds_.x + bounds_.w;
}

bool Window::onMenuRow(int x, int y) const {
    return hasBorder_ && hasMenuTabs() && y == bounds_.y + 1 && x >= bounds_.x && x < bounds_.x + bounds_.w;
}

bool Window::onBorder(int x, int y) const {
    if (!hasBorder_) return false;
    const int topRows = 1 + (hasMenuTabs() ? 1 : 0); // title (+ menu bar) = top
    const int y0 = bounds_.y;
    // Sides below the top (down to the row above the bottom) or the whole bottom row.
    const bool side = (x == bounds_.x || x == bounds_.x + bounds_.w - 1)
                   && y >= y0 + topRows && y < y0 + bounds_.h - 1;
    const bool bottom = y == y0 + bounds_.h - 1 && x >= bounds_.x && x <= bounds_.x + bounds_.w - 1;
    return side || bottom;
}

// Shared menu bar geometry - used both in draw() and in hit tests, so drawing
// and clicking agree on the targets.
// Menu bar = ITS OWN row under the title bar (classic window: title / menu / content).

Window::MenuStrip Window::menuStrip() const {
    MenuStrip out;
    if (!content_ || bounds_.w < kMinWidth) return out;
    const auto& tabs = content_->menuTabs();
    if (tabs.empty()) return out;

    // Logical tab offsets (no scroll): " Label " — 2 spaces + text.
    int total = 0;
    out.tabs.reserve(tabs.size());
    for (const auto& tab : tabs) {
        const int labW = 2 + static_cast<int>(tab.first.size());
        out.tabs.push_back({total, labW});
        total += labW;
    }

    const int inner = bounds_.w - 2; // between the window frames
    const int x0 = bounds_.x + 1;
    if (total <= inner) {
        // Fits — no buttons and no scrolling (as before).
        out.tabAreaX0 = x0;
        out.tabAreaX1 = x0 + inner;
        return out;
    }

    // Overflow: '<' at the left edge of the interior, '>' at the right,
    // tabs between them (scrolled horizontally).
    out.leftBtnX = x0;
    out.rightBtnX = x0 + inner - 1;
    out.tabAreaX0 = x0 + 1;
    out.tabAreaX1 = x0 + inner - 1;
    const int vis = out.tabAreaX1 - out.tabAreaX0;
    out.maxScroll = std::max(0, total - vis);
    out.scroll = std::clamp(menuScrollX_, 0, out.maxScroll);
    return out;
}

void Window::scrollMenuTabs(int dir) {
    const auto strip = menuStrip();
    if (strip.maxScroll <= 0 || strip.tabs.empty()) return;
    const int vis = strip.tabAreaX1 - strip.tabAreaX0;
    int ns = strip.scroll;
    if (dir > 0) {
        // Forward: to the start of the first tab sticking out past the right edge.
        for (const auto& [off, tw] : strip.tabs) {
            if (off + tw > strip.scroll + vis) { ns = off; break; }
        }
    } else {
        // Backward: to the start of the last tab hidden behind the left edge.
        int lastHidden = -1;
        for (const auto& [off, tw] : strip.tabs) {
            (void)tw;
            if (off < strip.scroll) lastHidden = off;
        }
        if (lastHidden >= 0) ns = lastHidden;
    }
    menuScrollX_ = std::clamp(ns, 0, strip.maxScroll);
}

void Window::ensureMenuTabVisible(int tab) {
    const auto strip = menuStrip();
    if (strip.maxScroll <= 0 || tab < 0 || tab >= static_cast<int>(strip.tabs.size())) return;
    const int vis = strip.tabAreaX1 - strip.tabAreaX0;
    const auto& [off, tw] = strip.tabs[static_cast<std::size_t>(tab)];
    int sx = strip.scroll;
    if (off < sx) sx = off;
    if (off + tw > sx + vis) sx = off + tw - vis;
    menuScrollX_ = std::clamp(sx, 0, strip.maxScroll);
}

int Window::menuTabScreenX(int tab) const {
    const auto strip = menuStrip();
    if (tab < 0 || tab >= static_cast<int>(strip.tabs.size())) return bounds_.x;
    return strip.tabAreaX0 + strip.tabs[static_cast<std::size_t>(tab)].first - strip.scroll;
}

int Window::menuTabIndexAt(int x, int y) const {
    if (!onMenuRow(x, y)) return -1;
    const auto strip = menuStrip();
    for (std::size_t i = 0; i < strip.tabs.size(); ++i) {
        const int ax = strip.tabAreaX0 + strip.tabs[i].first - strip.scroll;
        // Tab clickable only within the visible range (the <> cells are buttons).
        if (x >= ax && x < ax + strip.tabs[i].second &&
            x >= strip.tabAreaX0 && x < strip.tabAreaX1)
            return static_cast<int>(i);
    }
    return -1;
}

TitleBarZone Window::titleBarZoneAt(int x, int y) const {
    // Menu bar (its own row): <> buttons on overflow, tab = dropdown.
    if (onMenuRow(x, y)) {
        const auto strip = menuStrip();
        if (strip.leftBtnX >= 0 && x == strip.leftBtnX) return TitleBarZone::MenuScrollLeft;
        if (strip.rightBtnX >= 0 && x == strip.rightBtnX) return TitleBarZone::MenuScrollRight;
        if (menuTabIndexAt(x, y) >= 0) return TitleBarZone::MenuTab;
    }
    if (onTitleBar(x, y)) {
        const int closeX = bounds_.x + bounds_.w - 2;
        const int maxX = bounds_.x + bounds_.w - 3;
        const int minX = bounds_.x + bounds_.w - 4;
        if (bounds_.w >= kMinWidth) {
            if (x == closeX) return TitleBarZone::CloseButton;
            if (x == maxX) return TitleBarZone::MaximizeButton;
            if (x == minX) return TitleBarZone::MinimizeButton;
        }
        return TitleBarZone::Drag;
    }
    // Side/bottom edges (not the top) = Resize mode on click.
    if (onBorder(x, y)) return TitleBarZone::Border;
    return TitleBarZone::None;
}

void Window::relayoutContent() {
    if (!content_) return;
    if (hasBorder_) {
        // Content starts BELOW the title bar (and below the menu bar, if present).
        int contentY = bounds_.y + 1 + (hasMenuTabs() ? 1 : 0);
        int contentH = std::max(0, bounds_.y + bounds_.h - 1 - contentY);
        content_->setBounds(Rect{bounds_.x + 1, contentY, bounds_.w - 2, contentH});
    } else {
        content_->setBounds(bounds_);
    }
}

KeyEventResult Window::handleKey(const KeyEvent& ev) {
    if (content_) return content_->handleKey(ev);
    return KeyEventResult::Ignored;
}

void Window::draw(Screen& screen) {
    // Palette from the theme (theme::active):
    //   Border: white (focused) / gray (unfocused)
    //   Title bar: blue (titleBg/titleText)
    //   Content background: windowBg/windowText
    const auto& t = theme::active;
    const Color border = focused_ ? t.border : t.borderInactive;
    const Color titleBg = t.titleBg;
    const Color titleFg = t.titleText;
    const Color contentBg = t.windowBg;
    const Color contentFg = t.windowText;

    if (minimized_) return; // Minimized windows are represented only by taskbar handles.

    if (!hasBorder_) {
        if (content_) content_->draw(screen);
        return;
    }

    const int x0 = bounds_.x, y0 = bounds_.y, w = bounds_.w, h = bounds_.h;

    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            screen.set(x, y, Cell{U' ', border, t.desktopBg, 0});

    screen.set(x0, y0, Cell{U'┌', border, t.desktopBg, 0});
    screen.set(x0 + w - 1, y0, Cell{U'┐', border, t.desktopBg, 0});
    screen.set(x0, y0 + h - 1, Cell{U'└', border, t.desktopBg, 0});
    screen.set(x0 + w - 1, y0 + h - 1, Cell{U'┘', border, t.desktopBg, 0});
    for (int x = x0 + 1; x < x0 + w - 1; ++x) {
        screen.set(x, y0, Cell{U'─', border, t.desktopBg, 0});
        screen.set(x, y0 + h - 1, Cell{U'─', border, t.desktopBg, 0});
    }
    for (int y = y0 + 1; y < y0 + h - 1; ++y) {
        screen.set(x0, y, Cell{U'│', border, t.desktopBg, 0});
        screen.set(x0 + w - 1, y, Cell{U'│', border, t.desktopBg, 0});
    }

    // Title bar (row 1): window name on the left, [-] [+] [X] controls on the right.
    // Win95: blue background, white text.
    if (w >= kMinWidth) {
        // Title bar background
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, y0, Cell{' ', titleFg, titleBg, StyleBold});

        std::u32string titleText(U" ", 1);
        for (char c : title_)
            titleText += static_cast<char32_t>(static_cast<unsigned char>(c));
        titleText += U' ';
        int maxTitle = std::max(0, w - 2 - kButtonsWidth - 2);
        if (static_cast<int>(titleText.size()) > maxTitle) titleText.resize(static_cast<std::size_t>(maxTitle));
        screen.putText(x0 + 1, y0, titleText, titleFg, titleBg, StyleBold);

        // [-] [+]/[=] [X] controls — hit-tested by titleBarZoneAt().
        screen.set(x0 + w - 4, y0, Cell{'-', titleFg, titleBg, StyleBold});
        screen.set(x0 + w - 3, y0, Cell{static_cast<char32_t>(maximized_ ? U'=' : U'+'), titleFg, titleBg, StyleBold});
        screen.set(x0 + w - 2, y0, Cell{'X', t.closeText, t.closeBg, StyleBold});
    }

    // Menu bar (row 2, ITS OWN - as in windowing systems: title / menu / content).
    // Deliberately a different color than the blue title bar - menuBg background, menuText text.
    if (hasMenuTabs()) {
        int ym = y0 + 1;
        const Color menuBg = t.menuBg;
        const Color menuFg = t.menuText;
        // Empty menu cells: White/Gray - legible, cursor always visible
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, ym, Cell{' ', menuFg, menuBg, StyleBold});

        // On overflow the tabs scroll - <> at the interior edges.
        const auto strip = menuStrip();
        const auto& tabs = content_->menuTabs();
        for (std::size_t i = 0; i < strip.tabs.size(); ++i) {
            std::u32string lab(U" ", 1);
            lab += tabs[i].first;
            lab += U' ';
            const int ax = strip.tabAreaX0 + strip.tabs[i].first - strip.scroll;
            // Label drawn only within the visible tab range (clipping).
            for (std::size_t c = 0; c < lab.size(); ++c) {
                const int cx = ax + static_cast<int>(c);
                if (cx >= strip.tabAreaX0 && cx < strip.tabAreaX1)
                    screen.set(cx, ym, Cell{lab[c], menuFg, menuBg, StyleBold});
            }
        }
        if (strip.leftBtnX >= 0) {
            screen.set(strip.leftBtnX, ym, Cell{U'<', menuFg, menuBg, StyleBold});
            screen.set(strip.rightBtnX, ym, Cell{U'>', menuFg, menuBg, StyleBold});
        }
    }

    // Content background - Win95: Gray bg, White fg (consistent with the other Gray surfaces).
    for (int y = y0 + (hasMenuTabs() ? 2 : 1); y < y0 + h - 1; ++y)
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, y, Cell{U' ', contentFg, contentBg, 0});

    if (content_) content_->draw(screen);
}

} // namespace fenrir
