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

TitleBarZone Window::titleBarZoneAt(int x, int y) const {
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
        content_->setBounds(Rect{bounds_.x + 1, bounds_.y + 1,
                                  std::max(0, bounds_.w - 2), std::max(0, bounds_.h - 2)});
    } else {
        content_->setBounds(bounds_);
    }
}

bool Window::handleKey(const KeyEvent& ev) {
    if (content_) return content_->handleKey(ev);
    return false;
}

void Window::draw(Screen& screen) {
    const Color border = focused_ ? colors::Yellow : colors::Gray;
    const Color bg = colors::Black;

    if (minimized_) {
        // Zminimalizowane okno = tylko pasek tytulowy, bez tresci - widoczne
        // jako "zakladka" na pulpicie (pelny widok jest tez w pasku stanu).
        std::u32string bar = U"[_] " + std::u32string(title_.begin(), title_.end());
        screen.putText(bounds_.x, bounds_.y, bar, border, bg);
        return;
    }

    if (!hasBorder_) {
        if (content_) content_->draw(screen);
        return;
    }

    const int x0 = bounds_.x, y0 = bounds_.y, w = bounds_.w, h = bounds_.h;

    for (int y = y0; y < y0 + h; ++y)
        for (int x = x0; x < x0 + w; ++x)
            screen.set(x, y, Cell{U' ', border, bg, 0});

    screen.set(x0, y0, Cell{U'┌', border, bg, 0});
    screen.set(x0 + w - 1, y0, Cell{U'┐', border, bg, 0});
    screen.set(x0, y0 + h - 1, Cell{U'└', border, bg, 0});
    screen.set(x0 + w - 1, y0 + h - 1, Cell{U'┘', border, bg, 0});
    for (int x = x0 + 1; x < x0 + w - 1; ++x) {
        screen.set(x, y0, Cell{U'─', border, bg, 0});
        screen.set(x, y0 + h - 1, Cell{U'─', border, bg, 0});
    }
    for (int y = y0 + 1; y < y0 + h - 1; ++y) {
        screen.set(x0, y, Cell{U'│', border, bg, 0});
        screen.set(x0 + w - 1, y, Cell{U'│', border, bg, 0});
    }

    // Tytul - przycina sie tak, zeby nie wjechac na przyciski po prawej.
    int titleAreaEnd = (w >= kMinWidth) ? (x0 + w - 5) : (x0 + w - 1);
    std::u32string titleText = U" " + std::u32string(title_.begin(), title_.end()) + U" ";
    int maxTitle = std::max(0, titleAreaEnd - (x0 + 2));
    if (static_cast<int>(titleText.size()) > maxTitle) titleText.resize(static_cast<std::size_t>(maxTitle));
    screen.putText(x0 + 2, y0, titleText, bg, border, StyleBold);

    // Przyciski [_] [maks] [X] - hit-testowane przez titleBarZoneAt().
    if (w >= kMinWidth) {
        screen.set(x0 + w - 4, y0, Cell{U'_', bg, border, StyleBold});
        screen.set(x0 + w - 3, y0, Cell{maximized_ ? U'▾' : U'□', bg, border, StyleBold});
        screen.set(x0 + w - 2, y0, Cell{U'x', colors::White, colors::Red, StyleBold});
    }

    for (int y = y0 + 1; y < y0 + h - 1; ++y)
        for (int x = x0 + 1; x < x0 + w - 1; ++x)
            screen.set(x, y, Cell{U' ', colors::White, colors::Black, 0});

    if (content_) content_->draw(screen);
}

} // namespace fenrir
