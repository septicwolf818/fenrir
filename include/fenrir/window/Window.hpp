#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fenrir/widgets/Menu.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Three "buttons" in the top-right corner of the title bar, left to right:
// [_] minimize, [ ] maximize/restore, [X] close. The framework (Desktop)
// hit-tests them via these constants to know what to do on Enter/Space under the cursor.
enum class TitleBarZone { None, Drag, MinimizeButton, MaximizeButton, CloseButton, MenuTab,
                          MenuScrollLeft, MenuScrollRight, Border };

class Window {
public:
    Window(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool hasBorder = true);

    const std::string& title() const { return title_; }
    void setTitle(std::string t) { title_ = std::move(t); }

    const Rect& bounds() const { return bounds_; }
    void moveBy(int dx, int dy);
    void resizeBy(int dw, int dh);
    void moveTo(int x, int y);
    // Keep the window inside the usable desktop. If the terminal is smaller
    // than the content minimum, clip the window instead of letting it overflow.
    void constrainTo(const Rect& area);

    bool focused() const { return focused_; }
    // Changes the global active window. Content focus styling is retained when
    // this becomes inactive; the frame indicates global window focus.
    void setFocused(bool f);

    int zOrder() const { return zOrder_; }
    void setZOrder(int z) { zOrder_ = z; }

    bool minimized() const { return minimized_; }
    void setMinimized(bool m) { minimized_ = m; }

    bool maximized() const { return maximized_; }
    // area = desktop area (below the menu bar, above the status bar) to fill.
    void maximize(const Rect& area);
    void restore(); // undoes maximize() back to the previous bounds

    bool contains(int x, int y) const { return visibleBounds().contains(x, y); }
    Rect visibleBounds() const { return minimized_ ? Rect{} : bounds_; }
    bool onTitleBar(int x, int y) const;
    // Menu bar row (its own row under the title bar, as in windowing systems).
    bool onMenuRow(int x, int y) const;
    // Window edges: sides and bottom - NOT the top (title/menu have their own zones).
    // A virtual-cursor click on an edge enters Resize mode.
    bool onBorder(int x, int y) const;
    // Which title bar zone is under (x,y) - for GUI cursor handling.
    TitleBarZone titleBarZoneAt(int x, int y) const;
    // Which menu tab position (x,y) in the title bar maps to; -1 = none.
    int menuTabIndexAt(int x, int y) const;
    // Whether this window's title bar has menu tabs (quick check).
    bool hasMenuTabs() const { return content_ && content_->menuTabs().size() > 0; }
    // Scrolls the menu bar by one tab (<> buttons on overflow).
    void scrollMenuTabs(int dir);
    // Scrolls so the tab is visible (opening a dropdown).
    void ensureMenuTabVisible(int tab);
    // Screen column of a menu tab - for placing the dropdown (Desktop).
    int menuTabScreenX(int tab) const;

    Widget* content() { return content_.get(); }
    const Widget* content() const { return content_.get(); }

    void draw(Screen& screen);
    // Delegates the key to the content widget; returns a KeyEventResult.
    KeyEventResult handleKey(const KeyEvent& ev);

    // Recomputes the content widget's bounds relative to the window interior (under the frame/title).
    void relayoutContent();

private:
    std::string title_;
    Rect bounds_;
    Rect restoreBounds_{};
    std::unique_ptr<Widget> content_;
    bool hasBorder_;
    bool focused_ = false;
    int zOrder_ = 0;
    bool minimized_ = false;
    bool maximized_ = false;
    int menuScrollX_ = 0; // menu bar scrolling on overflow (columns)

    // Menu bar with scrolling: when tabs don't fit in the row,
    // <> buttons are drawn at the edges, and tabs scroll via menuScrollX_.
    struct MenuStrip {
        std::vector<std::pair<int,int>> tabs; // logical {offset from tabAreaX0, width}
        int leftBtnX = -1; // screen column of '<'; -1 = no buttons
        int rightBtnX = -1;  // column of '>'
        int tabAreaX0 = 0;   // visible tab range (between the buttons)
        int tabAreaX1 = 0;
        int scroll = 0;      // clamped scroll (columns)
        int maxScroll = 0;
    };
    MenuStrip menuStrip() const;

    static constexpr int kMinWidth = 12;
    static constexpr int kMinHeight = 4;
    static constexpr int kButtonsWidth = 6; // " _  X" ~ width reserved for the buttons

    // Minimum window SIZE derived from the content minimums (the widget's
    // minWidth/minHeight + frame/title bar + optional menu bar) - see Window.cpp.
    int minWindowWidth() const;
    int minWindowHeight() const;
};

} // namespace fenrir
