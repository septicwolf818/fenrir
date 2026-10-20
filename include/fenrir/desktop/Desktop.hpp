#pragma once
#include <chrono>
#include <functional>
#include <memory>
#include <optional>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/app/AppRegistry.hpp"
#include "fenrir/app/FileAssociationRegistry.hpp"
#include "fenrir/core/Screen.hpp"
#include "fenrir/cursor/Cursor.hpp"
#include "fenrir/input/KeyParser.hpp"
#include "fenrir/renderer/Renderer.hpp"
#include "fenrir/terminal/Terminal.hpp"
#include "fenrir/widgets/FileDialog.hpp"
#include "fenrir/widgets/Menu.hpp"
#include "fenrir/window/WindowManager.hpp"

namespace fenrir {

// Current mode of the custom GUI cursor / keyboard input:
//  - Desktop:       arrows move the free cursor anywhere on the screen;
//                   Enter/Space activates whatever is under the cursor.
//  - Keyboard:      inside the focused window - arrows move focus
//                   between widgets, Space/Enter activates them.
//  - Edit:          inside an editable widget (e.g. TextArea) - keys go
//                   straight to it (typing, arrows through the text);
//                   Escape falls back to that window's Keyboard mode.
//  - WindowMove:    arrows move the focused window.
//  - WindowResize:  arrows resize the focused window.
enum class CursorMode { Desktop, Keyboard, Edit, WindowMove, WindowResize };

// Desktop = the "desktop framework": custom cursor + modes, menu bar (top,
// dynamic width = terminal width), status bar (bottom),
// WindowManager ("window framework") and AppRegistry ("app framework").
// Applications implement App (see app/App.hpp) and touch nothing in this
// class beyond AppRegistry::registerApp().
class Desktop : public AppContext {
public:
    Desktop();

    AppRegistry& apps() { return registry_; }
    WindowManager& windowManager() { return wm_; }

    int screenWidth()  const { return screen_.width(); }
    int screenHeight() const { return screen_.height(); }
    // Desktop area available to windows: below the menu bar, above the status bar.
    Rect desktopArea() const;

    void setDesktopName(std::string name) { desktopName_ = std::move(name); }

    void quit() { running_ = false; }
    int run();

    // --- AppContext: API Open/OpenFolder/Save/Dialog ----------------------
    void openFile(const std::string& startDir, OnPick onPick, OnCancel onCancel) override;
    void openFolder(const std::string& startDir, OnPick onPick, OnCancel onCancel) override;
    void saveFile(const std::string& startDir, const std::string& suggestedName,
                  OnPick onPick, OnCancel onCancel) override;
    void openAssociatedFile(const std::string& path) override;
    void openFileWith(const std::string& path) override;
    void openDialog(std::u32string title, std::unique_ptr<Dialog> content,
                    OnDialogResult onResult) override;
    void closeWindow() override;
    // Asynchronous sources (e.g. the terminal PTY) — polled in tickOnce.
    void addFdSource(int fd, std::function<void()> onReady,
                     std::function<bool()> wantsWrite = {}) override;
    void removeFdSource(int fd) override;

private:
    Terminal terminal_;
    Screen screen_;
    Renderer renderer_;
    KeyParser keyParser_;
    Cursor cursor_;
    WindowManager wm_;
    AppRegistry registry_;
    FileAssociationRegistry fileAssociations_;
    bool fileAssociationsLoaded_ = false;

    std::string desktopName_ = "Fenrir";
    bool running_ = true;
    CursorMode mode_ = CursorMode::Desktop;
    // Cursor blink driven by a clock (steady_clock), NOT a tick counter:
    // a tick with the terminal open lasts ~30ms instead of ~400ms, so a tick
    // counter would blink a dozen times too fast.
    bool cursorBlinkState_ = true;
    std::chrono::steady_clock::time_point lastBlinkToggle_;

    // --- Taskbar state ----------------------------------------------------
    int taskbarScroll_{0};  // index of first visible handle on task bar
    static constexpr int kTaskHandleW = 12;  // fixed display width per window handle
    static constexpr int kMenuBarHeight = 1;
    static constexpr int kTaskBarHeight = 1;

    std::unique_ptr<Menu> appMenu_;  // != nullptr while the global application menu (F1) is open
    Rect appMenuBounds_{};
    bool appMenuClosePending_ = false;  // set by onSelect; handleKey closes the menu AFTER returning from the callback

    // Per-application menu (tabs in the window's title bar):
    // winMenu_ != nullptr while the dropdown is open for window winMenuWin_,
    // on tab winMenuTab_.
    Window* winMenuWin_{nullptr};
    int winMenuTab_ = 0;
    std::u32string winMenuTitle_{};
    std::unique_ptr<Menu> winMenu_;
    Rect winMenuBounds_{};
    bool winMenuClosePending_ = false;

    // Windows closed AFTER returning from a widget callback (dialogs). Closing a
    // window from inside its own handleKey would destroy the widget on its own
    // call stack — we flag it instead and Desktop closes after dispatch (see flushPendingCloses).
    std::vector<Window*> closePending_;

    struct FdSource {
        int fd;
        std::function<void()> onReady;
        std::function<bool()> wantsWrite;
    };
    // Registered fds are polled before keyboard input; active sources shorten
    // the input polling interval so child output appears smoothly.
    std::vector<FdSource> fdSources_;

    void handleKey(const KeyEvent& ev);          // wrapper: dispatch + flush of pending closes
    void handleKeyEvent(const KeyEvent& ev);     // actual key handling
    void flushPendingCloses();
    // Focus back to the window that opened the dialog (guard: does it still exist).
    void restoreFocusTo(Window* requester);
    // Reacts to a widget handleKey result: EnteredEdit -> Edit mode,
    // Ignored + Tab/Esc -> global fallbacks.
    void applyWidgetResult(KeyEventResult r, const KeyEvent& ev);

    void tickOnce();
    void loadFileAssociations();

    void openAppMenu();
    void closeAppMenu();
    // Per-application menu: opens (or switches to) the dropdown for tab
    // `tab` under the window's title bar. Tab = index into content->menuTabs().
    void openWinMenu(Window& w, int tab);
    void closeWinMenu();
    void buildWinMenu(Window& w, int tab);
    // Whether the window still exists in the manager (guard for raw pointers in callbacks).
    bool windowAlive(Window* w) const;
    void activateAtCursor();
    void launchApp(App& app);
    bool launchFile(App& app, const std::string& path);
    void toggleMaximize(Window& w);
    // Enter window-level keyboard navigation or editing mode.
    // Returns the new cursor mode (Keyboard or Edit).
    CursorMode enterWindowMode(Window& w);
    // Leave Keyboard/Edit mode on the desktop (does not drop the window's focus).
    void leaveWindowMode();
    void openDialogWindow(FileDialogMode mode, const std::string& startDir,
                          const std::string& suggestedName, OnPick onPick, OnCancel onCancel);

    void drawMenuBar(Screen& screen);
    void drawTaskBar(Screen& screen);
    void drawDesktopBackground(Screen& screen);
    void positionGuiCursor();

    // Helper functions for keyboard shortcut handling.
    static bool matchesShortcuts(const std::vector<std::pair<KeyShortcut, std::function<void()>>>& sc, const KeyEvent& ev);
    static void invokeShortcut(const std::vector<std::pair<KeyShortcut, std::function<void()>>>& sc, const KeyEvent& ev);
};

} // namespace fenrir
