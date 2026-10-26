#pragma once
#include <memory>
#include <string_view>
#include <vector>

#include "fenrir/window/Window.hpp"

namespace fenrir {

class WindowManager {
public:
    Window* createWindow(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool border = true);
    void closeWindow(Window* w);
    void closeFocused();

    void focus(Window* w);       // sets focus + raises to top (bringToFront)
    void focusNext();            // Tab
    void focusPrev();            // Shift+Tab
    Window* focused() const;

    Window* windowAt(int x, int y) const; // topmost z-order under (x,y); nullptr if desktop
    Widget* widgetAt(int x, int y) const;

    // Look up a window by its title (first match in z-order/back-to-front).
    Window* findWindow(std::string_view title) const;
    // True when a window with the given title currently exists.
    bool hasWindow(std::string_view title) const;
    // True when the given pointer still refers to a live window.
    bool containsWindow(const Window* w) const;

    void draw(Screen& screen); // draws all windows in z-order (back -> front)

    const std::vector<std::unique_ptr<Window>>& windows() const { return windows_; }

private:
    std::vector<std::unique_ptr<Window>> windows_;
    int nextZ_ = 1;

    void bringToFront(Window* w);
    void cycleFocus(int dir); // dir > 0 = next, dir < 0 = previous (skips minimized)
};

} // namespace fenrir
