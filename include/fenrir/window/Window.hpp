#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fenrir/widgets/Menu.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Trzy "przyciski" w prawym gornym rogu paska tytulu, w kolejnosci od lewej:
// [_] minimalizuj, [ ] maksymalizuj/przywroc, [X] zamknij. Framework (Desktop)
// hit-testuje wg tych stalych, zeby wiedziec co zrobic po Enter/Space pod kursorem.
enum class TitleBarZone { None, Drag, MinimizeButton, MaximizeButton, CloseButton, MenuTab };

// Shared tab-strip layout: returns {cursor_x, width} for each tab label in a menu strip.
// Used by Window::draw(), Window::menuTabIndexAt() hit-test, and Desktop::buildWinMenu().
std::vector<std::pair<int,int>> tabStripLayout(int x0, int w, const std::vector<MenuTab>& tabs);

class Window {
public:
    Window(std::string title, Rect bounds, std::unique_ptr<Widget> content, bool hasBorder = true);

    const std::string& title() const { return title_; }
    void setTitle(std::string t) { title_ = std::move(t); }

    const Rect& bounds() const { return bounds_; }
    void moveBy(int dx, int dy);
    void resizeBy(int dw, int dh);
    void moveTo(int x, int y);

    bool focused() const { return focused_; }
    void setFocused(bool f);

    int zOrder() const { return zOrder_; }
    void setZOrder(int z) { zOrder_ = z; }

    bool minimized() const { return minimized_; }
    void setMinimized(bool m) { minimized_ = m; }

    bool maximized() const { return maximized_; }
    // area = obszar pulpitu (pod paskiem menu, nad paskiem stanu) do wypelnienia.
    void maximize(const Rect& area);
    void restore(); // cofa maximize() do poprzednich granic

    bool contains(int x, int y) const { return visibleBounds().contains(x, y); }
    Rect visibleBounds() const { return minimized_ ? Rect{bounds_.x, bounds_.y, bounds_.w, 1} : bounds_; }
    bool onTitleBar(int x, int y) const;
    // Wiersz paska menu (wlasny wiersz pod paskiem tytulu, jak w systemach okienkowych).
    bool onMenuRow(int x, int y) const;
    // Ktora strefa paska tytulu jest pod (x,y) - do obslugi kursora GUI.
    TitleBarZone titleBarZoneAt(int x, int y) const;
    // Ktorej karcie menu odpowiada pozycja (x,y) paska tytulu; -1 = zadnej.
    int menuTabIndexAt(int x, int y) const;
    // Czy pasek tytulu tego okna zawiera karty menu (do szybkiego sprawdzenia).
    bool hasMenuTabs() const { return content_ && content_->menuTabs().size() > 0; }

    Widget* content() { return content_.get(); }
    const Widget* content() const { return content_.get(); }

    void draw(Screen& screen);
    // Deleguje klawisz do widgetu-zawartosci; zwraca KeyEventResult.
    KeyEventResult handleKey(const KeyEvent& ev);

    // Przelicza granice widgetu-zawartosci wzgledem wnetrza okna (pod ramka/tytulem).
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

    static constexpr int kMinWidth = 12;
    static constexpr int kMinHeight = 4;
    static constexpr int kButtonsWidth = 6; // " _  X" ~ szerokosc rezerwowana pod przyciski
};

} // namespace fenrir
