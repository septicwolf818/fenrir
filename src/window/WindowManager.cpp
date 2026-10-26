#include "fenrir/window/WindowManager.hpp"

#include <algorithm>

namespace fenrir {

Window* WindowManager::createWindow(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool border) {
    auto w = std::make_unique<Window>(std::move(title), bounds, std::move(content), border);
    Window* raw = w.get();
    windows_.push_back(std::move(w));
    focus(raw);
    return raw;
}

void WindowManager::closeWindow(Window* w) {
    auto it = std::find_if(windows_.begin(), windows_.end(),
                            [&](const std::unique_ptr<Window>& p) { return p.get() == w; });
    if (it == windows_.end()) return;
    bool wasFocused = w->focused();
    windows_.erase(it);
    if (!wasFocused || windows_.empty()) return;

    // New focus = the window ON TOP (highest zOrder), not the last one
    // created — closing the top window must not reveal a buried one.
    Window* top = nullptr;
    for (auto& p : windows_)
        if (!p->minimized() && (!top || p->zOrder() > top->zOrder())) top = p.get();
    if (!top)
        for (auto& p : windows_)
            if (!top || p->zOrder() > top->zOrder()) top = p.get();
    focus(top);
}

void WindowManager::closeFocused() {
    if (Window* f = focused()) closeWindow(f);
}

void WindowManager::bringToFront(Window* w) {
    w->setZOrder(nextZ_++);
}

void WindowManager::focus(Window* w) {
    for (auto& p : windows_) p->setFocused(p.get() == w);
    if (w) {
        w->setMinimized(false); // focusing restores a minimized window
        bringToFront(w);
    }
}

Window* WindowManager::focused() const {
    for (auto& p : windows_)
        if (p->focused()) return p.get();
    return nullptr;
}

void WindowManager::focusNext() { cycleFocus(+1); }

void WindowManager::focusPrev() { cycleFocus(-1); }

void WindowManager::cycleFocus(int dir) {
    // Alt+Tab cycles only through VISIBLE windows: minimized windows are
    // reachable from the taskbar, not by cycling (focus() would otherwise
    // restore one, defeating the point of minimizing it).
    std::vector<Window*> ordered;
    for (auto& p : windows_)
        if (!p->minimized()) ordered.push_back(p.get());
    if (ordered.empty()) return;
    std::sort(ordered.begin(), ordered.end(), [](Window* a, Window* b) { return a->zOrder() < b->zOrder(); });

    auto it = std::find_if(ordered.begin(), ordered.end(), [](Window* w) { return w->focused(); });
    std::size_t n = ordered.size();
    std::size_t idx = (it == ordered.end())
                          ? 0
                          : (static_cast<std::size_t>(it - ordered.begin()) + n + (dir > 0 ? 1 : n - 1)) % n;
    focus(ordered[idx]);
}

Window* WindowManager::windowAt(int x, int y) const {
    Window* best = nullptr;
    int bestZ = -1;
    for (auto& p : windows_) {
        if (p->contains(x, y) && p->zOrder() > bestZ) {
            best = p.get();
            bestZ = p->zOrder();
        }
    }
    return best;
}

Window* WindowManager::findWindow(std::string_view title) const {
    for (const auto& p : windows_) {
        if (p->title() == title) return p.get();
    }
    return nullptr;
}

bool WindowManager::hasWindow(std::string_view title) const {
    return findWindow(title) != nullptr;
}

bool WindowManager::containsWindow(const Window* w) const {
    if (!w) return false;
    for (const auto& p : windows_) {
        if (p.get() == w) return true;
    }
    return false;
}

Widget* WindowManager::widgetAt(int x, int y) const {
    Window* w = windowAt(x, y);
    return w ? w->content() : nullptr;
}

void WindowManager::draw(Screen& screen) {
    std::vector<Window*> ordered;
    for (auto& p : windows_) ordered.push_back(p.get());
    std::sort(ordered.begin(), ordered.end(), [](Window* a, Window* b) { return a->zOrder() < b->zOrder(); });
    for (Window* w : ordered) w->draw(screen);
}

} // namespace fenrir
