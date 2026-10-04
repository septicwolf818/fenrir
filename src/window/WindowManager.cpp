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
    if (wasFocused && !windows_.empty()) focus(windows_.back().get());
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
        w->setMinimized(false); // fokusowanie przywraca zminimalizowane okno
        bringToFront(w);
    }
}

Window* WindowManager::focused() const {
    for (auto& p : windows_)
        if (p->focused()) return p.get();
    return nullptr;
}

void WindowManager::focusNext() {
    if (windows_.empty()) return;
    std::vector<Window*> ordered;
    for (auto& p : windows_) ordered.push_back(p.get());
    std::sort(ordered.begin(), ordered.end(), [](Window* a, Window* b) { return a->zOrder() < b->zOrder(); });

    auto it = std::find_if(ordered.begin(), ordered.end(), [](Window* w) { return w->focused(); });
    std::size_t idx = (it == ordered.end()) ? 0 : (static_cast<std::size_t>(it - ordered.begin()) + 1) % ordered.size();
    focus(ordered[idx]);
}

void WindowManager::focusPrev() {
    if (windows_.empty()) return;
    std::vector<Window*> ordered;
    for (auto& p : windows_) ordered.push_back(p.get());
    std::sort(ordered.begin(), ordered.end(), [](Window* a, Window* b) { return a->zOrder() < b->zOrder(); });

    auto it = std::find_if(ordered.begin(), ordered.end(), [](Window* w) { return w->focused(); });
    std::size_t n = ordered.size();
    std::size_t idx = (it == ordered.end()) ? 0
                                             : (static_cast<std::size_t>(it - ordered.begin()) + n - 1) % n;
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
