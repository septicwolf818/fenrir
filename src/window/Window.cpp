#include "fenrir/window/Window.hpp"

#include <algorithm>

#include "fenrir/core/Text.hpp"
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

bool Window::onBorder(int x, int y) const {
    if (!hasBorder_) return false;
    const int topRows = 1 + (hasMenuTabs() ? 1 : 0); // tytul (+ pasek menu) = gorа
    const int y0 = bounds_.y;
    // Boki ponizej gory (do wiersza nad dolnym) lub caly dolny wiersz.
    const bool side = (x == bounds_.x || x == bounds_.x + bounds_.w - 1)
                   && y >= y0 + topRows && y < y0 + bounds_.h - 1;
    const bool bottom = y == y0 + bounds_.h - 1 && x >= bounds_.x && x <= bounds_.x + bounds_.w - 1;
    return side || bottom;
}

// Wspolna geometria paska menu — uzywana i w draw(), i w hit-testach,
// zeby rysowanie i klikanie sie zgadzaly co do celi.
// Pasek menu = WLASNY wiersz pod paskiem tytulu (kласyczne okno: tytul / menu / tresc).

Window::MenuStrip Window::menuStrip() const {
    MenuStrip out;
    if (!content_ || bounds_.w < kMinWidth) return out;
    const auto& tabs = content_->menuTabs();
    if (tabs.empty()) return out;

    // Logiczne offsety kart (bez scrolla): " Etykieta " — 2 spacje + tekst.
    int total = 0;
    out.tabs.reserve(tabs.size());
    for (const auto& tab : tabs) {
        const int labW = 2 + static_cast<int>(tab.first.size());
        out.tabs.push_back({total, labW});
        total += labW;
    }

    const int inner = bounds_.w - 2; // miedzy ramkami okna
    const int x0 = bounds_.x + 1;
    if (total <= inner) {
        // Miesci sie — bez przyciskow i scrolla (jak wczesniej).
        out.tabAreaX0 = x0;
        out.tabAreaX1 = x0 + inner;
        return out;
    }

    // Przepełnienie: '<' przy lewej krawedzi wnetrza, '>' przy prawej,
    // karty miedzy nimi (przewijane poziomo).
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
        // Do przodu: do poczatku pierwszej karty wystajacej za prawa krawedz.
        for (const auto& [off, tw] : strip.tabs) {
            if (off + tw > strip.scroll + vis) { ns = off; break; }
        }
    } else {
        // Do tylu: do poczatku ostatniej karty ukrytej za lewa krawedzia.
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
        // Karta klikalna tylko w widocznym zakresie (komorki <> sa przyciskami).
        if (x >= ax && x < ax + strip.tabs[i].second &&
            x >= strip.tabAreaX0 && x < strip.tabAreaX1)
            return static_cast<int>(i);
    }
    return -1;
}

TitleBarZone Window::titleBarZoneAt(int x, int y) const {
    // Pasek menu (wlasny wiersz): przyciski <> przy przepełnieniu, karta = dropdown.
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
    // Krawedzie boczne/dolna (bez gory) = tryb Resize przy kliknieciu.
    if (onBorder(x, y)) return TitleBarZone::Border;
    return TitleBarZone::None;
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
        // toUtf32, bo std::u32string(bajty) daje mojibake dla liter spoza ASCII.
        std::u32string bar = U"[_] " + text::toUtf32(title_);
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

        // Przy przepełnieniu karty sie przewijaja — <> na krawędziach wnetrza.
        const auto strip = menuStrip();
        const auto& tabs = content_->menuTabs();
        for (std::size_t i = 0; i < strip.tabs.size(); ++i) {
            std::u32string lab(U" ", 1);
            lab += tabs[i].first;
            lab += U' ';
            const int ax = strip.tabAreaX0 + strip.tabs[i].first - strip.scroll;
            // Etykieta rysowana tylko w widocznym zakresie kart (przyciecie).
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

    // Tło tresci — Win95: Gray bg, White fg (spójne z pozostałymi powierzchniami Gray).
    for (int y = y0 + (hasMenuTabs() ? 2 : 1); y < y0 + h - 1; ++y)
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, y, Cell{U' ', contentFg, contentBg, 0});

    if (content_) content_->draw(screen);
}

} // namespace fenrir
