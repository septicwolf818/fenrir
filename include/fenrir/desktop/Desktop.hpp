#pragma once
#include <functional>
#include <memory>
#include <optional>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/app/AppRegistry.hpp"
#include "fenrir/core/Screen.hpp"
#include "fenrir/cursor/Cursor.hpp"
#include "fenrir/input/KeyParser.hpp"
#include "fenrir/renderer/Renderer.hpp"
#include "fenrir/terminal/Terminal.hpp"
#include "fenrir/widgets/FileDialog.hpp"
#include "fenrir/widgets/Menu.hpp"
#include "fenrir/window/WindowManager.hpp"

namespace fenrir {

// Tryb, w jakim aktualnie dziala wlasny kursor GUI / klawiatura:
//  - Desktop:       strzalki przesuwaja wolny kursor po calym ekranie;
//                    Enter/Space aktywuje to, co jest pod kursorem.
//  - Keyboard:      wewnatrz fokusowanego okna - strzalki przemieszczaja
//                    focus pomiedzy widgetami, Space/Enter aktywuje je.
//  - Edit:          wewnatrz edytowalnego widgetu (np. TextArea) - klawisze
//                    trafiaja bezposrednio do niego (pisanie, strzalki po tekscie);
//                    Escape cofa do Keyboard mode tego okna.
//  - WindowMove:     strzalki przesuwaja fokusowane okno.
//  - WindowResize:   strzalki zmieniaja rozmiar fokusowanego okna.
enum class CursorMode { Desktop, Keyboard, Edit, WindowMove, WindowResize };

// Desktop = "framework pulpitu": wlasny kursor + tryby, pasek menu (u gory,
// dynamiczna szerokosc = szerokosc terminala), pasek stanu (u dolu),
// WindowManager ("framework okien") i AppRegistry ("framework aplikacji").
// Aplikacje implementuja App (patrz app/App.hpp) i nie dotykaja niczego
// z tej klasy poza AppRegistry::registerApp().
class Desktop : public AppContext {
public:
    Desktop();

    AppRegistry& apps() { return registry_; }
    WindowManager& windowManager() { return wm_; }

    int screenWidth()  const { return screen_.width(); }
    int screenHeight() const { return screen_.height(); }
    // Obszar pulpitu dostepny na okna: pod paskiem menu, nad paskiem stanu.
    Rect desktopArea() const;

    void setDesktopName(std::string name) { desktopName_ = std::move(name); }

    void quit() { running_ = false; }
    int run();

    // --- AppContext: API Open/OpenFolder/Save ---------------------------
    void openFile(const std::string& startDir, OnPick onPick, OnCancel onCancel) override;
    void openFolder(const std::string& startDir, OnPick onPick, OnCancel onCancel) override;
    void saveFile(const std::string& startDir, const std::string& suggestedName,
                  OnPick onPick, OnCancel onCancel) override;

private:
    Terminal terminal_;
    Screen screen_;
    Renderer renderer_;
    KeyParser keyParser_;
    Cursor cursor_;
    WindowManager wm_;
    AppRegistry registry_;

    std::string desktopName_ = "Fenrir";
    bool running_ = true;
    CursorMode mode_ = CursorMode::Desktop;
    bool cursorBlinkState_ = true;

    // --- Taskbar state ----------------------------------------------------
    int taskbarScroll_{0};  // index of first visible handle on task bar
    static constexpr int kTaskHandleW = 12;  // fixed display width per window handle
    static constexpr int kMenuBarHeight = 1;
    static constexpr int kTaskBarHeight = 1;

    std::unique_ptr<Menu> appMenu_;  // != nullptr gdy globalne menu aplikacji (F1) jest otwarte
    Rect appMenuBounds_{};
    bool appMenuClosePending_ = false;  // onSelect ustawia; handleKey domyka menu PO powrocie z callbacku

    // Menu konkretnej aplikacji (zakładki w pasku tytułu okna):
    // winMenu_ != nullptr gdy dropdown jest otwarty dla okna winMenuWin_,
    // na karcie winMenuTab_.
    Window* winMenuWin_{nullptr};
    int winMenuTab_ = 0;
    std::u32string winMenuTitle_{};
    std::unique_ptr<Menu> winMenu_;
    Rect winMenuBounds_{};
    bool winMenuClosePending_ = false;

    void tickOnce();
    void handleKey(const KeyEvent& ev);

    void openAppMenu();
    void closeAppMenu();
    // Menu konkretnej aplikacji: otwiera (lub przełącza na) dropdown karty
    // `tab` pod paskiem tytułu okna. Tab = indeks w content->menuTabs().
    void openWinMenu(Window& w, int tab);
    void closeWinMenu();
    void buildWinMenu(Window& w, int tab);
    // Czy okno wciąż istnieje w managerze (guard dla raw pointerów w callbackach).
    bool windowAlive(Window* w) const;
    void activateAtCursor();
    void launchApp(App& app);
    void toggleMaximize(Window& w);
    // Wejście w tryb nawigacji kl. lub edycji na poziomie okna.
    // Zwraca nowy tryb kursora (Keyboard lub Edit).
    CursorMode enterWindowMode(Window& w);
    // Wyjscie z trybu Keyboard/Edit na pulpitu (nie niszczy fokusu okna).
    void leaveWindowMode();
    void openDialogWindow(FileDialogMode mode, const std::string& startDir,
                          const std::string& suggestedName, OnPick onPick, OnCancel onCancel);

    void drawMenuBar(Screen& screen);
    void drawTaskBar(Screen& screen);
    void drawDesktopBackground(Screen& screen);
    void positionGuiCursor();

    // Pomocnicze funkcje do obslugi skrótow klawiszowych.
    static bool matchesShortcuts(const std::vector<std::pair<KeyShortcut, std::function<void()>>>& sc, const KeyEvent& ev);
    static void invokeShortcut(std::vector<std::pair<KeyShortcut, std::function<void()>>> sc, const KeyEvent& ev);
};

} // namespace fenrir
