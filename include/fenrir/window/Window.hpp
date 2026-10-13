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
    // Krawedzie okna: boki i dol — BEZ gory (tytul/menu maja wlasne strefy).
    // Klik wirtualnym kursorem na krawedzi otwiera tryb Resize.
    bool onBorder(int x, int y) const;
    // Ktora strefa paska tytulu jest pod (x,y) - do obslugi kursora GUI.
    TitleBarZone titleBarZoneAt(int x, int y) const;
    // Ktorej karcie menu odpowiada pozycja (x,y) paska tytulu; -1 = zadnej.
    int menuTabIndexAt(int x, int y) const;
    // Czy pasek tytulu tego okna zawiera karty menu (do szybkiego sprawdzenia).
    bool hasMenuTabs() const { return content_ && content_->menuTabs().size() > 0; }
    // Przesuwa pasek menu o jedna karte (przyciski <> przy przepełnieniu).
    void scrollMenuTabs(int dir);
    // Ustawia scroll tak, zeby karta byla widoczna (otwieranie dropdownu).
    void ensureMenuTabVisible(int tab);
    // Ekranowa kolumna karty menu — do umieszczenia dropdownu (Desktop).
    int menuTabScreenX(int tab) const;

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
    int menuScrollX_ = 0; // przewijanie paska menu przy przepełnieniu (kolumny)

    // Pasek menu z obsluga przewijania: gdy karty nie mieszcza sie w wierszu,
    // rysowane sa przyciski <> na krawędziach, a karty przewija menuScrollX_.
    struct MenuStrip {
        std::vector<std::pair<int,int>> tabs; // logiczne {offset od tabAreaX0, szerokosc}
        int leftBtnX = -1; // kolumna '<' na ekranie; -1 = brak przyciskow
        int rightBtnX = -1;  // kolumna '>'
        int tabAreaX0 = 0;   // widoczny zakres kart (miedzy przyciskami)
        int tabAreaX1 = 0;
        int scroll = 0;      // sklamlowany scroll (kolumny)
        int maxScroll = 0;
    };
    MenuStrip menuStrip() const;

    static constexpr int kMinWidth = 12;
    static constexpr int kMinHeight = 4;
    static constexpr int kButtonsWidth = 6; // " _  X" ~ szerokosc rezerwowana pod przyciski

    // Minimalny rozmiar OKNA wynikajacy z minimow tresci (minWidth/minHeight
    // widgetu + ramka/pasek tytulu + ewentualny pasek menu) — patrz Window.cpp.
    int minWindowWidth() const;
    int minWindowHeight() const;
};

} // namespace fenrir
