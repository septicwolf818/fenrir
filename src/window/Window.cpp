#include "fenrir/window/Window.hpp"

#include <algorithm>

#include "fenrir/core/Theme.hpp"

namespace fenrir {

Window::Window(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool hasBorder)
    : title_(std::move(title)), bounds_(bounds), content_(std::move(content)), hasBorder_(hasBorder) {
    // Okno nie moze powstac mniejsze, niz potrzebuje tresc (preferredBounds
    // aplikacji bywaja przyciete na malych terminalach).
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
    // Podczas resize (F6) obowiazuje tez minimum wynikajace z tresci —
    // okno nie da sie zeszpecic ponizej minWidth/minHeight widgetu.
    bounds_.w = std::max(minWindowWidth(), bounds_.w + dw);
    bounds_.h = std::max(minWindowHeight(), bounds_.h + dh);
    relayoutContent();
}

int Window::minWindowWidth() const {
    const int contentMin = content_ ? content_->minWidth() : 0;
    return std::max(kMinWidth, contentMin + (hasBorder_ ? 2 : 0));
}

int Window::minWindowHeight() const {
    // relayoutContent(): contentH = h - 1 - (1 + menu) dla ramki,
    // czyli h = contentH + 2 + (menu ? 1 : 0); bez ramki h = contentH.
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
    // Na makro-terminalach area bywa mniejsza od minimum tresci — jak
    // resizeBy, przycinamy do minWindow*, zeby tresc nie byla obcinana.
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

// Wspolna geometria paska menu — uzywana i w draw(), i w hit-testach,
// zeby rysowanie i klikanie sie zgadzaly co do celi.
// Pasek menu = WLASNY wiersz pod paskiem tytulu (kласyczne okno: tytul / menu / tresc).

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
    // Paleta z motywu (theme::active):
    //   Border: bialy (focused) / szary (unfocused)
    //   Pasek tytulu: niebieski (titleBg/titleText)
    //   Tlo tresci: windowBg/windowText
    const auto& t = theme::active;
    const Color border = focused_ ? t.border : t.borderInactive;
    const Color titleBg = t.titleBg;
    const Color titleFg = t.titleText;
    const Color contentBg = t.windowBg;
    const Color contentFg = t.windowText;

    if (minimized_) {
        // Zminimalizowane okno = tylko pasek tytulowy, bez tresci - widoczne
        // jako "zakladka" na pulpicie (pelny widok jest tez w pasku stanu).
        std::u32string bar = U"[_] " + std::u32string(title_.begin(), title_.end());
        screen.putText(bounds_.x, bounds_.y, bar, border, t.desktopBg);
        return;
    }

    if (!hasBorder_) {
        if (content_) content_->draw(screen);
        return;
    }

    const int x0 = bounds_.x, y0 = bounds_.y, w = bounds_.w, h = bounds_.h;

    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            screen.set(x, y, Cell{U' ', border, t.desktopBg, 0});

    screen.set(x0, y0, Cell{'+', border, t.desktopBg, 0});
    screen.set(x0 + w - 1, y0, Cell{'+', border, t.desktopBg, 0});
    screen.set(x0, y0 + h - 1, Cell{'+', border, t.desktopBg, 0});
    screen.set(x0 + w - 1, y0 + h - 1, Cell{'+', border, t.desktopBg, 0});
    for (int x = x0 + 1; x < x0 + w - 1; ++x) {
        screen.set(x, y0, Cell{'-', border, t.desktopBg, 0});
        screen.set(x, y0 + h - 1, Cell{'-', border, t.desktopBg, 0});
    }
    for (int y = y0 + 1; y < y0 + h - 1; ++y) {
        screen.set(x0, y, Cell{'|', border, t.desktopBg, 0});
        screen.set(x0 + w - 1, y, Cell{'|', border, t.desktopBg, 0});
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
        screen.set(x0 + w - 3, y0, Cell{static_cast<char32_t>(maximized_ ? U'/' : U' '), titleFg, titleBg, StyleBold});
        screen.set(x0 + w - 2, y0, Cell{'x', t.closeText, t.closeBg, StyleBold});
    }

    // Pasek menu (wiersz 2, WLASNY — jak w systemach okienkowych: tytul / menu / tresc).
    // Celowo inna barwa niz niebieski pasek tytulu — menuBg tlo, menuText tekst.
    if (hasMenuTabs()) {
        int ym = y0 + 1;
        const Color menuBg = t.menuBg;
        const Color menuFg = t.menuText;
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
