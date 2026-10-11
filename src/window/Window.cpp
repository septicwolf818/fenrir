#include "fenrir/window/Window.hpp"

#include <algorithm>

namespace fenrir {

Window::Window(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool hasBorder)
    : title_(std::move(title)), bounds_(bounds), content_(std::move(content)), hasBorder_(hasBorder) {
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
    bounds_.w = std::max(kMinWidth, bounds_.w + dw);
    bounds_.h = std::max(kMinHeight, bounds_.h + dh);
    relayoutContent();
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
    bounds_ = area;
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

// Wspolna geometria paska menu — uzywana i w draw(), i w hit-testach,
// zeby rysowanie i klikanie sie zgadzaly co do celi.
// Pasek menu = WLASNY wiersz pod paskiem tytulu (kласyczne okno: tytul / menu / tresc).
constexpr int kButtonsWidth = 6; // " _  X" + luki — identycznie jak w Window.

std::vector<std::pair<int, int>> tabStripLayout(int x0, int w, const std::vector<MenuTab>& tabs) {
    std::vector<std::pair<int, int>> out;
    if (w < 12 || tabs.empty()) return out;

    // Karty od lewej krawedzi wiersza menu.
    int cursor = x0 + 1;
    for (auto& tab : tabs) {
        int labW = 2 + static_cast<int>(tab.first.size());
        if (cursor + labW > x0 + w - 1) break;
        out.push_back({cursor, labW});
        cursor += labW;
    }
    return out;
}

int Window::menuTabIndexAt(int x, int y) const {
    if (!onMenuRow(x, y)) return -1;
    auto strips = tabStripLayout(bounds_.x, bounds_.w, content_ ? content_->menuTabs() : std::vector<MenuTab>{});
    for (std::size_t i = 0; i < strips.size(); ++i) {
        if (x >= strips[i].first && x < strips[i].first + strips[i].second)
            return static_cast<int>(i);
    }
    return -1;
}

TitleBarZone Window::titleBarZoneAt(int x, int y) const {
    // Pasek menu (wlasny wiersz): karta pod kursorem otwiera dropdown.
    if (menuTabIndexAt(x, y) >= 0) return TitleBarZone::MenuTab;
    if (!onTitleBar(x, y)) return TitleBarZone::None;
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

void Window::relayoutContent() {
    if (!content_) return;
    if (hasBorder_) {
        // Tresc zaczyna sie POD paskiem tytulu (i pod paskiem menu, jesli jest).
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
    // Win95 palette:
    //   Border: White (focused) / Gray (unfocused)
    //   Title bar + menu bar: Blue bg, White fg  (jedyne niebieskie elementy okna)
    //   Body (tlo pod widgetami): Gray bg
    //   Listy: White bg, Black fg; selekcja: Gray bg
    const Color border = focused_ ? colors::White : colors::Gray;
    const Color titleBg = colors::Blue;
    const Color titleFg = colors::White;
    const Color contentBg = colors::Gray;
    const Color contentFg = colors::White;

    if (minimized_) {
        // Zminimalizowane okno = tylko pasek tytulowy, bez tresci - widoczne
        // jako "zakladka" na pulpicie (pelny widok jest tez w pasku stanu).
        std::u32string bar = U"[_] " + std::u32string(title_.begin(), title_.end());
        screen.putText(bounds_.x, bounds_.y, bar, border, colors::Black);
        return;
    }

    if (!hasBorder_) {
        if (content_) content_->draw(screen);
        return;
    }

    const int x0 = bounds_.x, y0 = bounds_.y, w = bounds_.w, h = bounds_.h;

    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            screen.set(x, y, Cell{U' ', border, colors::Black, 0});

    screen.set(x0, y0, Cell{'+', border, colors::Black, 0});
    screen.set(x0 + w - 1, y0, Cell{'+', border, colors::Black, 0});
    screen.set(x0, y0 + h - 1, Cell{'+', border, colors::Black, 0});
    screen.set(x0 + w - 1, y0 + h - 1, Cell{'+', border, colors::Black, 0});
    for (int x = x0 + 1; x < x0 + w - 1; ++x) {
        screen.set(x, y0, Cell{'-', border, colors::Black, 0});
        screen.set(x, y0 + h - 1, Cell{'-', border, colors::Black, 0});
    }
    for (int y = y0 + 1; y < y0 + h - 1; ++y) {
        screen.set(x0, y, Cell{'|', border, colors::Black, 0});
        screen.set(x0 + w - 1, y, Cell{'|', border, colors::Black, 0});
    }

    // Pasek tytulowy (wiersz 1): nazwa okna po lewej, przyciski [_] [ ] [X] po prawej.
    // Win95: niebieskie tlo, biały tekst.
    if (w >= kMinWidth) {
        // Tło paska tytułu
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, y0, Cell{' ', titleFg, titleBg, StyleBold});

        std::u32string titleText(U" ", 1);
        for (char c : title_)
            titleText += static_cast<char32_t>(static_cast<unsigned char>(c));
        titleText += U' ';
        int maxTitle = std::max(0, w - 2 - kButtonsWidth - 2);
        if (static_cast<int>(titleText.size()) > maxTitle) titleText.resize(static_cast<std::size_t>(maxTitle));
        screen.putText(x0 + 1, y0, titleText, titleFg, titleBg, StyleBold);

        // Przyciski [_] [ ] [X] - hit-testowane przez titleBarZoneAt().
        screen.set(x0 + w - 4, y0, Cell{'_', titleFg, titleBg, StyleBold});
        screen.set(x0 + w - 3, y0, Cell{maximized_ ? '/' : ' ', titleFg, titleBg, StyleBold});
        screen.set(x0 + w - 2, y0, Cell{'x', colors::White, colors::Red, StyleBold});
    }

    // Pasek menu (wiersz 2, WLASNY — jak w systemach okienkowych: tytul / menu / tresc).
    // Celowo inna barwa niz niebieski pasek tytulu — Gray tlo, White tekst.
    if (hasMenuTabs()) {
        int ym = y0 + 1;
        const Color menuBg = colors::Gray;
        const Color menuFg = colors::White;
        // puste miejsca menu: White/Gray — czytelne, kursor zawsze widoczny
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, ym, Cell{' ', menuFg, menuBg, StyleBold});

        auto strips = tabStripLayout(x0, w, content_->menuTabs());
        for (std::size_t i = 0; i < strips.size(); ++i) {
            std::u32string lab(U" ", 1);
            lab += content_->menuTabs()[i].first;
            lab += U' ';
            screen.putText(strips[i].first, ym, lab, menuFg, menuBg, StyleBold);
        }
    }

    // Tło tresci — Win95: Gray bg, White fg (spójne z pozostałymi powierzchniami Gray).
    for (int y = y0 + (hasMenuTabs() ? 2 : 1); y < y0 + h - 1; ++y)
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, y, Cell{U' ', contentFg, contentBg, 0});

    if (content_) content_->draw(screen);
}

} // namespace fenrir
