#include "fenrir/desktop/Desktop.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <poll.h>
#include <signal.h>
#include <atomic>
#include <string>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Settings.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/SelectionList.hpp"
#include "fenrir/window/Window.hpp"

namespace {
std::atomic<bool> g_shutdownRequested{false};
extern "C" void fenrirSignalHandler(int) { g_shutdownRequested.store(true); }
} // namespace

namespace fenrir {

namespace {

class OpenWithMenu final : public Menu {
public:
    using Menu::Menu;
    std::function<void()> onCancel;

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.key == Key::Escape) {
            if (onCancel) onCancel();
            return KeyEventResult::Consumed;
        }
        return Menu::handleKey(ev);
    }
};

class NotificationCenterView final : public SelectionList {
public:
    NotificationCenterView() { refresh(); }

    void refresh() {
        auto& bus = notifications::global();
        ids_.clear();
        std::vector<std::u32string> rows;
        for (const auto& n : bus.items()) {
            ids_.push_back(n.id);
            std::u32string row = (n.read ? U"  " : U"* ");
            row += text::toUtf32(n.source.empty() ? std::string("app") : n.source);
            row += U": ";
            row += text::toUtf32(n.title);
            if (!n.body.empty()) {
                row += U" - ";
                row += text::toUtf32(n.body);
            }
            rows.push_back(row);
        }
        setItems(rows);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.key == Key::Delete || ev.key == Key::Backspace) {
            const int idx = selectedIndex();
            if (idx >= 0 && idx < static_cast<int>(ids_.size())) {
                notifications::global().remove(ids_[static_cast<std::size_t>(idx)]);
                refresh();
                return KeyEventResult::Consumed;
            }
        }
        return SelectionList::handleKey(ev);
    }

private:
    std::vector<int> ids_;
};

void drawBox(Screen& screen, const Rect& r, std::u32string_view title) {
    // Dropdown frame from the theme: blue title bar (titleBg/titleText),
    // gray frame (menuBg/menuText), white interior (surfaceBg/surfaceText).
    const auto& t = theme::active;
    const Color titleBg = t.titleBg;
    const Color titleFg = t.titleText;
    const Color frameBg = t.menuBg;
    const Color frameFg = t.menuText;
    const Color innerFg = t.surfaceText;
    const Color innerBg = t.surfaceBg;

    // Interior — white (the menu fills it with entries; leftover space stays white).
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            screen.set(x, y, Cell{U' ', innerFg, innerBg, 0});

    // Top row = blue title bar.
    for (int x = r.x; x < r.x + r.w; ++x)
        screen.set(x, r.y, Cell{U' ', titleFg, titleBg, StyleBold});

    // Box-drawing frame: single-cell UTF-8 glyphs available in standard TTY fonts.
    screen.set(r.x, r.y, Cell{U'┌', frameFg, titleBg, 0});
    screen.set(r.x + r.w - 1, r.y, Cell{U'┐', frameFg, titleBg, 0});
    for (int y = r.y + 1; y < r.y + r.h - 1; ++y) {
        screen.set(r.x, y, Cell{U'│', frameFg, frameBg, 0});
        screen.set(r.x + r.w - 1, y, Cell{U'│', frameFg, frameBg, 0});
    }
    screen.set(r.x, r.y + r.h - 1, Cell{U'└', frameFg, frameBg, 0});
    screen.set(r.x + r.w - 1, r.y + r.h - 1, Cell{U'┘', frameFg, frameBg, 0});
    for (int x = r.x + 1; x < r.x + r.w - 1; ++x)
        screen.set(x, r.y + r.h - 1, Cell{U'─', frameFg, frameBg, 0});

    // Title text on the title bar.
    if (!title.empty()) {
        std::u32string t = U" " + std::u32string(title) + U" ";
        screen.putText(r.x + 1, r.y, t, titleFg, titleBg, StyleBold);
    }
}

// Dropdown frame width fitted to the content, always wide enough for the
// widest column: frame + padding + text (label OR bar title) + padding
// + frame = +4. Minimum 12 so an empty dropdown stays legible.
int dropdownWidth(const std::vector<std::u32string>& labels, std::u32string_view title) {
    int w = 12;
    for (const auto& l : labels)
        w = std::max<int>(w, static_cast<int>(l.size()) + 4);
    w = std::max<int>(w, static_cast<int>(title.size()) + 4);
    return w;
}

// Window-close guard: a window with dirty content (Widget::dirty) does not
// close immediately — we ask the user (AppContext::confirm = OK/Cancel dialog)
// and enqueue the close only on OK; on Cancel/Esc nothing happens.
// `deferClose` always enqueues the close (closePending_), even for a clean
// window: the dialog callback runs from inside the widget's handleKey, so the
// window may only die in flushPendingCloses — after returning from dispatch.
// Both paths thus close the window after the key press (or click) is handled.
void closeWithConfirm(AppContext& ctx, Window* w, std::function<void()> deferClose) {
    if (!w) return;
    if (!w->content() || !w->content()->dirty()) { deferClose(); return; }
    ctx.confirm(U"Unsaved changes. Close the window?",
                [deferClose = std::move(deferClose)](bool ok) {
                    if (ok) deferClose();
                });
}

} // namespace

// Helper functions for keyboard shortcut handling — static because they do not modify Desktop state.

bool Desktop::matchesShortcuts(const std::vector<std::pair<KeyShortcut, std::function<void()>>>& sc, const KeyEvent& ev) {
    for (auto& [shortcut, _] : sc) {
        if (shortcut.ctrl != ev.ctrl) continue;
        if (shortcut.alt != ev.alt) continue;
        if (shortcut.key != ev.key) continue;
        // For Key::Char we compare the character — note: the terminal sends lowercase!
        if (shortcut.key == Key::Char) {
            if (shortcut.character.has_value()) {
                char32_t sc = shortcut.character.value();
                // Case-insensitive match for letters (the terminal always sends lowercase).
                char32_t ec = ev.character;
                if (sc != ec && std::tolower(static_cast<unsigned char>(sc)) != std::tolower(static_cast<unsigned char>(ec))) continue;
            } else {
                // No character specified = no match (nothing to compare against).
                continue;
            }
        }
        return true;
    }
    return false;
}

void Desktop::invokeShortcut(const std::vector<std::pair<KeyShortcut, std::function<void()>>>& sc, const KeyEvent& ev) {
    for (auto& [shortcut, cb] : sc) {
        if (shortcut.ctrl != ev.ctrl) continue;
        if (shortcut.alt != ev.alt) continue;
        if (shortcut.key != ev.key) continue;
        if (shortcut.key == Key::Char) {
            if (!shortcut.character.has_value()) continue;
            char32_t sc = shortcut.character.value();
            char32_t ec = ev.character;
            if (sc != ec && std::tolower(static_cast<unsigned char>(sc)) != std::tolower(static_cast<unsigned char>(ec))) continue;
        }
        if (cb) cb();
    }
}

Desktop::Desktop()
    : terminal_(),
      screen_(terminal_.size().columns, terminal_.size().rows),
      renderer_(terminal_),
      wm_(),
      registry_() {
    // Persistent settings (~/.config/fenrir/settings.ini): theme, name,
    // clock, blinking cursor. Loaded before the first draw.
    settings::load();
    theme::apply(settings::active.theme);
    desktopName_ = settings::active.desktopName;
    // The clipboard lives in a file (shared by all Fenrir processes); on top
    // of that the copied text is pushed to the terminal with OSC 52, so
    // graphical terminals that support it can paste it outside of Fenrir.
    clipboard::setTerminalSink([this](const std::string& utf8) { pushClipboardToTerminal(utf8); });
}

void Desktop::pushClipboardToTerminal(const std::string& utf8) {
    // OSC 52 (clipboard access): ESC ] 52 ; c ; <base64> BEL
    terminal_.write("\x1b]52;c;" + clipboard::base64Encode(utf8) + "\x07");
    terminal_.flush();
}

Rect Desktop::desktopArea() const {
    int h = std::max(0, screenHeight() - kMenuBarHeight - kTaskBarHeight);
    return Rect{0, kMenuBarHeight, screenWidth(), h};
}

void Desktop::openAppMenu() {
    if (appMenu_) return;
    closeWinMenu(); // the app dropdown and the desktop menu are never open at the same time
    appMenuClosePending_ = false; // clear stale flags from the previous close (e.g. after launching an app)

    // Applications are listed alphabetically by name, not in registration order.
    std::vector<App*> ordered;
    for (auto& a : registry_.apps()) ordered.push_back(a.get());
    std::sort(ordered.begin(), ordered.end(), [](const App* a, const App* b) {
        return a->name() < b->name();
    });

    std::vector<std::u32string> items;
    for (auto* a : ordered) {
        std::string n = a->name();
        items.emplace_back(n.begin(), n.end());
    }
    items.push_back(U"Quit desktop");

    int w = std::max(24, dropdownWidth(items, U"Applications"));
    int h = static_cast<int>(items.size()) + 2;
    appMenuBounds_ = Rect{1, kMenuBarHeight + 1, w, h}; // 1 row gap below the bar - aligned with the first row of windows

    auto menu = std::make_unique<Menu>(items);
    Menu* raw = menu.get();
    raw->setBounds(Rect{appMenuBounds_.x + 1, appMenuBounds_.y + 1, appMenuBounds_.w - 2, appMenuBounds_.h - 2});
    appMenu_ = std::move(menu);

    raw->onSelect = [this, ordered](int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < ordered.size()) {
            launchApp(*ordered[static_cast<std::size_t>(index)]);
        } else if (index == static_cast<int>(ordered.size())) {
            quit();
        }
        // Close AFTER returning from the callback — reset() destroys the Menu, so we
        // cannot remove it from inside its own onSelect (UB on std::function).
        appMenuClosePending_ = true;
    };
}

void Desktop::closeAppMenu() {
    appMenu_.reset();
    appMenuClosePending_ = false; // a stale flag after closing would close the next menu on the first key press
}

void Desktop::performShutdown() {
    settings::save();
    notifications::global().save();
    lastSave_ = std::chrono::steady_clock::now();
}

void Desktop::openNotificationCenter() {
    auto& bus = notifications::global();
    bus.markAllRead();

    Rect area = desktopArea();
    Rect bounds{area.x + 2, area.y + 1, std::min(60, std::max(30, area.w - 4)),
                std::min(16, std::max(6, area.h - 3))};
    auto view = std::make_unique<NotificationCenterView>();
    view->refresh();
    Window* w = wm_.createWindow("Notifications", bounds, std::move(view));
    w->constrainTo(area);
    wm_.focus(w);
    cursor_.moveTo(w->bounds().x + 2, w->bounds().y + 1);
    cursor_.clamp(screenWidth(), screenHeight());
    mode_ = enterWindowMode(*w);
}

void Desktop::openBackgroundTasks() {
    std::u32string text;
    const auto names = backgroundWorkers();
    if (names.empty()) {
        text = U"No background tasks running.";
    } else {
        for (const auto& n : names) {
            text += U"* ";
            text += text::toUtf32(n);
            text += U"\n";
        }
    }
    showMessage(std::move(text));
}

// --- Per-application menu (dropdown under the window's title bar) -------------------

bool Desktop::windowAlive(Window* w) const {
    for (auto& c : wm_.windows())
        if (c.get() == w) return true;
    return false;
}

void Desktop::openWinMenu(Window& w, int tab) {
    auto* content = w.content();
    if (!content) return;
    auto& tabs = content->menuTabs();
    if (tabs.empty()) return;
    if (tab < 0 || tab >= static_cast<int>(tabs.size())) tab = 0;

    appMenu_.reset(); // the dropdown and the desktop menu (F1) are mutually exclusive

    // Switching tabs in an open menu = switching to that tab.
    if (winMenu_ && winMenuWin_ == &w && winMenuTab_ == tab) return;

    winMenuWin_ = &w;
    winMenuTab_ = tab;
    winMenuClosePending_ = false;
    buildWinMenu(w, tab);
}

void Desktop::closeWinMenu() {
    winMenu_.reset();
    winMenuWin_ = nullptr;
    winMenuClosePending_ = false;
}

void Desktop::buildWinMenu(Window& w, int tab) {
    auto* content = w.content();
    if (!content) { closeWinMenu(); return; }
    auto& tabs = content->menuTabs();
    if (tab < 0 || tab >= static_cast<int>(tabs.size())) { closeWinMenu(); return; }

    auto& items = tabs[static_cast<std::size_t>(tab)].second;

    std::vector<std::u32string> labels;
    labels.reserve(items.size());
    for (auto& it : items) labels.push_back(it.label);

    // Dropdown BELOW the window's menu row (menu row = y+1), under the given tab.
    // The keyboard can open a menu tab that is off-screen — scroll it into view first.
    int h = static_cast<int>(labels.size()) + 2;
    int menuW = dropdownWidth(labels, tabs[static_cast<std::size_t>(tab)].first);

    Rect wb = w.bounds();
    w.ensureMenuTabVisible(tab);
    int x = w.menuTabScreenX(tab) + 1;
    int y = wb.y + 2; // below the title bar (y+0) and menu bar (y+1)
    // Keep the dropdown within the screen.
    x = std::max(0, std::min(x, screenWidth() - menuW));
    y = std::max(kMenuBarHeight, std::min(y, screenHeight() - kTaskBarHeight - h));

    winMenuTitle_ = tabs[static_cast<std::size_t>(tab)].first;
    winMenuBounds_ = Rect{x, y, menuW, h};

    auto menu = std::make_unique<Menu>(std::move(labels));
    Menu* raw = menu.get();
    raw->setBounds(Rect{winMenuBounds_.x + 1, winMenuBounds_.y + 1,
                         winMenuBounds_.w - 2, winMenuBounds_.h - 2});
    winMenu_ = std::move(menu);

    // Snapshot: the callback may close the window (e.g. an "Exit" item), so after
    // it the winMenuWin_ pointer may dangle — we keep labels + callbacks
    // BY VALUE in the closure and never go back to the window for data.
    auto itemsCopy = items; // value-capture: std::function copies it safely
    raw->onSelect = [this, items = std::move(itemsCopy)](int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < items.size() &&
            items[static_cast<std::size_t>(index)].cb) {
            items[static_cast<std::size_t>(index)].cb();
        }
        winMenuClosePending_ = true; // close AFTER returning from the callback (never destroy the Menu mid-call)
    };
}

// --- Dialogs: closing windows AFTER returning from widget callbacks -------------------

void Desktop::flushPendingCloses() {
    if (closePending_.empty()) return;
    std::vector<Window*> pending = std::move(closePending_);
    closePending_.clear();
    for (Window* w : pending)
        if (windowAlive(w)) wm_.closeWindow(w);
    renderer_.forceFullRedraw();
}

void Desktop::restoreFocusTo(Window* requester) {
    if (requester && windowAlive(requester)) {
        wm_.focus(requester);
        mode_ = enterWindowMode(*requester);
    } else {
        // The requesting window was closed in the meantime — we do not touch
        // the (dangling) pointer, we just return to the desktop.
        mode_ = CursorMode::Desktop;
    }
}

void Desktop::applyWidgetResult(KeyEventResult r, const KeyEvent& ev) {
    if (r == KeyEventResult::EnteredEdit) { mode_ = CursorMode::Edit; return; }
    if (r != KeyEventResult::Ignored) return;
    // The widget does not want this key — the global fallback is ONLY Escape
    // (unwinding Edit -> Keyboard -> Desktop). Plain Tab deliberately does
    // nothing: window switching is exclusively via Alt+Tab.
    if (ev.key == Key::Escape) {
        if (mode_ == CursorMode::Edit) mode_ = CursorMode::Keyboard;
        else leaveWindowMode();
    }
}

void Desktop::launchApp(App& app) {
    Rect area = desktopArea();
    Rect bounds = app.preferredBounds(area);
    Window* w = wm_.createWindow(app.name(), bounds, app.createView(*this));
    w->constrainTo(area);
    wm_.focus(w);
    cursor_.moveTo(w->bounds().x + 2, w->bounds().y + 1);
    cursor_.clamp(screenWidth(), screenHeight());
    // Enter Keyboard mode - the user navigates between widgets with arrows
    // and activates them with Enter/Space. Escape -> Desktop.
    mode_ = enterWindowMode(*w);
}

bool Desktop::launchFile(App& app, const std::string& path) {
    auto view = app.createFileView(*this, path);
    if (!view) return false;
    Rect area = desktopArea();
    Rect bounds = app.preferredBounds(area);
    Window* window = wm_.createWindow(app.name(), bounds, std::move(view));
    window->constrainTo(area);
    wm_.focus(window);
    cursor_.moveTo(window->bounds().x + 2, window->bounds().y + 1);
    cursor_.clamp(screenWidth(), screenHeight());
    mode_ = enterWindowMode(*window);
    return true;
}

void Desktop::loadFileAssociations() {
    if (fileAssociationsLoaded_) return;
    fileAssociations_.load(registry_);
    fileAssociationsLoaded_ = true;
}

void Desktop::openAssociatedFile(const std::string& path) {
    loadFileAssociations();
    const std::string appId = fileAssociations_.appIdForPath(path);
    App* app = registry_.findById(appId);
    if (!app || !launchFile(*app, path)) openFileWith(path);
}

void Desktop::openFileWith(const std::string& path) {
    loadFileAssociations();
    const auto apps = registry_.fileApps();
    if (apps.empty()) {
        showMessage(U"No installed app can open files.");
        return;
    }

    const std::string extension = FileAssociationRegistry::extensionForPath(path);
    const std::string currentId = fileAssociations_.appIdForPath(path);
    std::vector<std::u32string> labels;
    labels.reserve(apps.size());
    for (const App* app : apps) {
        std::u32string label = text::toUtf32(app->name());
        if (app->id() == currentId) label += U" (default)";
        labels.push_back(std::move(label));
    }

    const std::u32string title = extension.empty()
        ? U"Open with"
        : U"Open with ." + text::toUtf32(extension);
    const int width = std::min(52, std::max(24, dropdownWidth(labels, title) + 2));
    const int height = std::min(std::max(6, static_cast<int>(labels.size()) + 3),
                                std::max(6, desktopArea().h));
    const Rect area = desktopArea();
    Rect bounds{area.x + std::max(0, (area.w - width) / 2),
                area.y + std::max(0, (area.h - height) / 2), width, height};

    Window* requester = wm_.focused();
    auto menu = std::make_unique<OpenWithMenu>(std::move(labels));
    OpenWithMenu* raw = menu.get();
    Window* picker = wm_.createWindow(text::toUtf8(title), bounds, std::move(menu));
    picker->constrainTo(area);
    wm_.focus(picker);
    mode_ = enterWindowMode(*picker);

    raw->onSelect = [this, path, apps, picker](int index) {
        if (index < 0 || static_cast<std::size_t>(index) >= apps.size()) return;
        App* selected = apps[static_cast<std::size_t>(index)];
        const bool saved = fileAssociations_.associateFile(path, selected->id(), registry_);
        if (!launchFile(*selected, path)) return;
        closePending_.push_back(picker);
        if (!saved) showMessage(U"Could not save the file association.");
    };
    raw->onCancel = [this, picker, requester] {
        if (wm_.focused() == picker) restoreFocusTo(requester);
        closePending_.push_back(picker);
    };
}

CursorMode Desktop::enterWindowMode(Window& w) {
    if (!w.content()) return CursorMode::Keyboard;
    return w.content()->editable() ? CursorMode::Edit : CursorMode::Keyboard;
}

void Desktop::leaveWindowMode() {
    // Remove the highlight from all widgets in the focused window.
    if (Window* f = wm_.focused()) {
        f->content()->setHighlighted(false);
    }
    mode_ = CursorMode::Desktop;
}

void Desktop::openDialogWindow(FileDialogMode mode, const std::string& startDir,
                                const std::string& suggestedName, OnPick onPick, OnCancel onCancel) {
    std::string dir = fs::isDirectory(startDir) ? startDir : fs::parent(startDir);
    if (!fs::isDirectory(dir)) dir = "/";

    Window* requester = wm_.focused();

    auto dialog = std::make_unique<FileDialog>(mode, dir, suggestedName);
    FileDialog* raw = dialog.get();

    Rect area = desktopArea();
    Rect bounds{area.x + 2, area.y + 1, std::min(56, std::max(24, area.w - 4)),
                std::min(16, std::max(8, area.h - 2))};
    std::string title = mode == FileDialogMode::SaveFile  ? "Save As"
                         : mode == FileDialogMode::OpenFolder ? "Select Folder"
                                                               : "Open File";
    Window* w = wm_.createWindow(title, bounds, std::move(dialog));
    w->constrainTo(area);
    wm_.focus(w);
    renderer_.forceFullRedraw();
    mode_ = enterWindowMode(*w);

    // Deferred close (closePending_): the callback is invoked from inside the
    // widget's handleKey — we cannot destroy the window on its own call stack.
    // Focus returns to the requester, but ONLY if it still exists.
    raw->onConfirm = [this, w, requester, onPick = std::move(onPick)](const std::string& path) {
        if (onPick) onPick(path);
        // the cb may have opened another dialog (e.g. guardDirty -> openFile) — then
        // the new window owns focus and we must not restore it to the requester,
        // we would go back to the old source and leave the new dialog unfocused
        // (it would ignore keys). We only restore focus if nobody else took it.
        if (wm_.focused() == w) restoreFocusTo(requester);
        closePending_.push_back(w);
    };
    raw->onCancel = [this, w, requester, onCancel = std::move(onCancel)]() {
        if (onCancel) onCancel();
        if (wm_.focused() == w) restoreFocusTo(requester);
        closePending_.push_back(w);
    };
}

void Desktop::openFile(const std::string& startDir, OnPick onPick, OnCancel onCancel) {
    openDialogWindow(FileDialogMode::OpenFile, startDir, "", std::move(onPick), std::move(onCancel));
}

void Desktop::openFolder(const std::string& startDir, OnPick onPick, OnCancel onCancel) {
    openDialogWindow(FileDialogMode::OpenFolder, startDir, "", std::move(onPick), std::move(onCancel));
}

void Desktop::saveFile(const std::string& startDir, const std::string& suggestedName,
                        OnPick onPick, OnCancel onCancel) {
    // "Save As" on an existing file: we ask for confirmation instead of silently
    // overwriting (until now FileDialog::confirmSave returned the path unchecked,
    // and a truncating save (TextArea/Paint) happened without asking). Ctrl+S on
    // an open file (saveToCurrentPath) does NOT go through this dialog - the
    // confirmation applies only to "Save As" flows.
    // requester = the requesting window (app): the save dialog may close
    // before the user resolves the overwrite confirmation.
    Window* requester = wm_.focused();

    // guard = shared "overwrite gate": reopening the save dialog (after Cancel)
    // loops through the same guard, so every subsequent choice is checked too.
    // The OnPick trampolines hold it (copying the shared_ptr);
    // the weak ptr BEFORE the guard lambda breaks the guard -> lambda -> guard cycle.
    using GuardFn = std::function<void(const std::string&)>;
    auto guard = std::make_shared<GuardFn>();
    *guard = [this, requester, startDir, suggestedName, onPick, onCancel,
              weak = std::weak_ptr<GuardFn>(guard)](const std::string& path) {
        if (!fs::exists(path)) {
            // Focus goes straight to the requesting window (the real requester may
            // already be dead if this is a reopened save dialog); the
            // openDialogWindow wrapper sees the new focus and will not restore the old one.
            restoreFocusTo(requester);
            if (onPick) onPick(path);
            return;
        }
        // The file already exists - ask whether to overwrite. keep = strong handle
        // for the confirmation callback: the save dialog (with the guard trampoline)
        // will be closed via closePending_ before the confirmation resolves.
        auto keep = weak.lock();
        const std::u32string msg =
            U"File " + text::toUtf32(fs::nameOf(path)) + U" already exists. Overwrite?";
        confirm(msg, [this, requester, path, startDir, suggestedName, onPick, onCancel,
                      keep](bool ok) {
            if (ok) {
                // Focus goes straight to the requesting window (the save dialog is
                // already gone); BEFORE onPick - if the app opens its own dialog
                // (e.g. a save error), it gets focus after ours.
                restoreFocusTo(requester);
                if (onPick) onPick(path);
            } else {
                // Overwrite cancelled: reopen "Save As" with the same
                // directory, suggested name and overwrite gate, so the user can pick
                // another name (looping through the guard).
                OnCancel cancelCopy{onCancel}; // onCancel in the lambda is const
                openDialogWindow(FileDialogMode::SaveFile, startDir, suggestedName,
                                 OnPick([keep](const std::string& p) { (*keep)(p); }),
                                 std::move(cancelCopy));
            }
        });
    };
    openDialogWindow(FileDialogMode::SaveFile, startDir, suggestedName,
                     OnPick([guard](const std::string& p) { (*guard)(p); }),
                     std::move(onCancel));
}

void Desktop::closeWindow() {
    // File > Exit in apps: the same dirty-guard as Ctrl+W (a dirty window
    // asks for confirmation; the close itself is always deferred past the
    // menu callback via closePending_).
    Window* w = wm_.focused();
    closeWithConfirm(*this, w, [this, w] { closePending_.push_back(w); });
}

void Desktop::notify(std::string source, std::string title, std::string body) {
    if (!settings::active.appNotifications) return;
    notifications::global().push(std::move(source), std::move(title), std::move(body));
}

notifications::Bus& Desktop::notifications() {
    return notifications::global();
}

void Desktop::registerBackgroundWorker(std::string name, AppContext::BackgroundTick tick) {
    if (!tick) return;
    for (auto& w : backgroundWorkers_) {
        if (w.name == name) { w.tick = std::move(tick); return; }
    }
    backgroundWorkers_.push_back({std::move(name), std::move(tick)});
}

void Desktop::unregisterBackgroundWorker(const std::string& name) {
    std::erase_if(backgroundWorkers_, [&name](const BackgroundWorker& w) { return w.name == name; });
}

std::vector<std::string> Desktop::backgroundWorkers() const {
    std::vector<std::string> out;
    for (const auto& w : backgroundWorkers_) out.push_back(w.name);
    return out;
}

void Desktop::openDialog(std::u32string title, std::unique_ptr<Dialog> content, OnDialogResult onResult) {
    Window* requester = wm_.focused();
    Dialog* raw = content.get();

    // Size from the dialog itself (wrapped message + field + buttons), centered.
    // WARNING: The dialog lives INSIDE a framed window — Window::relayoutContent
    // removes the title bar, bottom frame (h-2), and one column from each side
    // (w-2). heightForWidth calculates for the CONTENT width (as draw wraps
    // at bounds.w-2), so the window needs that width plus the frame and that
    // height plus the frame. Otherwise the message does not fit at all (only
    // the buttons were visible because the draw loop skipped the message).
    Rect area = desktopArea();
    const int w = std::min(56, std::max(30, area.w - 6)); // window width (including frame)
    const int contentW = w - 2;
    const int contentH = raw ? raw->heightForWidth(contentW) : 10;
    int h = contentH + 2; // title bar + bottom frame (dialog window without tabs)
    h = std::min(area.h - 2, std::max(7, h));
    Rect bounds{area.x + std::max(0, (area.w - w) / 2),
                area.y + std::max(0, (area.h - h) / 2), w, h};

    Window* win = wm_.createWindow(text::toUtf8(title), bounds, std::move(content));
    win->constrainTo(area);
    wm_.focus(win);
    renderer_.forceFullRedraw();
    mode_ = enterWindowMode(*win);

    // Widget callback: result -> app callback -> focus -> DEFERRED window close
    // (the widget stays alive until handleKey returns; see flushPendingCloses).
    raw->onResult = [this, win, requester, cb = std::move(onResult)](int idx) {
        if (cb) cb(idx);
        // Same exception as in openDialogWindow: the callback may open another
        // dialog, which then owns focus, so do not call restoreFocusTo.
        if (wm_.focused() == win) restoreFocusTo(requester);
        closePending_.push_back(win);
    };
}

void Desktop::toggleMaximize(Window& w) {
    if (w.maximized()) w.restore();
    else w.maximize(desktopArea());
    w.constrainTo(desktopArea());
}

void Desktop::activateAtCursor() {
    // Leave appMenu_ (the F1 menu) alone — it is a separate global menu.
    if (!appMenu_) {
        // Any click closes the open application dropdown, EXCEPT when clicking
        // the same tab that is already open (avoid flicker).
        if (winMenu_ && winMenuWin_ && windowAlive(winMenuWin_)) {
            bool overSameTab = winMenuWin_->titleBarZoneAt(cursor_.x(), cursor_.y()) == TitleBarZone::MenuTab
                              && winMenuWin_->menuTabIndexAt(cursor_.x(), cursor_.y()) == winMenuTab_;
            if (!overSameTab) closeWinMenu();
        } else if (winMenu_) {
            closeWinMenu(); // The window is gone — clear the state.
        }
    }

    if (cursor_.y() < kMenuBarHeight) { openAppMenu(); return; }

    // --- Taskbar (status bar) — activate a window handle -------------------
    if (cursor_.y() >= screenHeight() - kTaskBarHeight) {
        const auto& wins = wm_.windows();
        int totalHandles = static_cast<int>(wins.size());
        if (totalHandles == 0) return; // nothing to activate

        int canScrollLeft  = taskbarScroll_ > 0;
        int rightArrowWidth = 1;
        int usableWidth    = screenWidth() - (canScrollLeft ? 1 : 0) - rightArrowWidth;
        int maxVisible     = std::max(1, usableWidth / kTaskHandleW);

        // Check whether a scroll arrow was clicked.
        if (cursor_.x() == 0 && canScrollLeft) {
            taskbarScroll_ = std::max(0, taskbarScroll_ - 1);
            return;
        }
        int arrowX = screenWidth() - 1;
        if (cursor_.x() == arrowX && (taskbarScroll_ + maxVisible < totalHandles)) {
            taskbarScroll_ += 1;
            return;
        }

        // Calculate the index of the window under the cursor.
        int visualStartX = canScrollLeft ? 1 : 0;
        int winIdx       = (cursor_.x() - visualStartX) / kTaskHandleW;
        winIdx += taskbarScroll_;

        if (winIdx >= 0 && winIdx < totalHandles) {
            Window* w = wins[static_cast<std::size_t>(winIdx)].get();
            wm_.focus(w);                       // focus() restores minimized windows
            mode_ = enterWindowMode(*w);         // enter window mode
        }
        return;
    }

    if (cursor_.y() >= screenHeight() - kTaskBarHeight) return; // informational taskbar

    Window* w = wm_.windowAt(cursor_.x(), cursor_.y());
    if (!w) return; // empty desktop — nothing under the cursor

    if (w->minimized()) { wm_.focus(w); return; } // focus() restores it

    wm_.focus(w);
    switch (w->titleBarZoneAt(cursor_.x(), cursor_.y())) {
        case TitleBarZone::CloseButton:
            // Guard: dirty content prompts before closing (as with Ctrl+W).
            closeWithConfirm(*this, w, [this, w] { closePending_.push_back(w); });
            break;
        case TitleBarZone::MinimizeButton: w->setMinimized(true); break;
        case TitleBarZone::MaximizeButton: toggleMaximize(*w); break;
        case TitleBarZone::Drag: mode_ = CursorMode::WindowMove; break;
        case TitleBarZone::MenuTab:
            openWinMenu(*w, w->menuTabIndexAt(cursor_.x(), cursor_.y()));
            break;
        case TitleBarZone::MenuScrollLeft:  w->scrollMenuTabs(-1); break;
        case TitleBarZone::MenuScrollRight: w->scrollMenuTabs(+1); break;
        case TitleBarZone::Border:
            // Window border (except the top) enters Resize mode, as with F6.
            mode_ = CursorMode::WindowResize;
            break;
        case TitleBarZone::None:
            // Content click: the widget may handle it (e.g. an editor tab);
            // otherwise, enter the window's standard interaction mode.
            if (w->content() && w->content()->clickAt(cursor_.x(), cursor_.y())) break;
            mode_ = enterWindowMode(*w);
            break;
    }
}

void Desktop::handleKey(const KeyEvent& ev) {
    // Dispatch the key, THEN close windows deferred by dialog callbacks
    // (the widget must have returned from its handleKey).
    handleKeyEvent(ev);
    flushPendingCloses();
}

void Desktop::handleKeyEvent(const KeyEvent& ev) {
    // Ctrl+Q ALWAYS works, regardless of cursor/menu mode — it is the emergency
    // exit. Without it, the user could get stuck in WindowMove mode, for example,
    // with no way to leave the application.
    if (ev.ctrl && ev.key == Key::Char && ev.character == U'q') {
        confirm(U"Quit Fenrir?", [this](bool ok) {
            if (ok) { performShutdown(); quit(); }
        });
        return;
    }

    // F12 is the non-destructive escape hatch from apps that consume Esc (most
    // importantly a nested Fenrir inside the Terminal). Leave the current mode,
    // but keep every window and its running processes alive.
    if (ev.key == Key::F12) {
        closeAppMenu();
        closeWinMenu();
        if (mode_ != CursorMode::Desktop) leaveWindowMode();
        return;
    }

    // --- Desktop menu (F1) has the highest priority ---
    if (appMenu_) {
        if (ev.key == Key::Escape) { closeAppMenu(); return; }
        appMenu_->handleKey(ev);
        if (appMenuClosePending_) closeAppMenu(); // After onSelect returns — avoids undefined behavior.
        return;
    }

    // --- Application menu (dropdown below the window title bar) ---
    if (winMenu_) {
        if (ev.key == Key::Escape) { closeWinMenu(); return; }
        Window* owner = winMenuWin_;
        auto nTabs = (owner && windowAlive(owner) && owner->content())
                         ? static_cast<int>(owner->content()->menuTabs().size())
                         : 0;
        // Left/Right switches tabs when there is more than one.
        if (nTabs > 1) {
            if (ev.key == Key::Left)  { openWinMenu(*owner, (winMenuTab_ - 1 + nTabs) % nTabs); return; }
            if (ev.key == Key::Right) { openWinMenu(*owner, (winMenuTab_ + 1) % nTabs); return; }
        }
        winMenu_->handleKey(ev);
        if (winMenuClosePending_) closeWinMenu(); // After onSelect returns — avoids undefined behavior.
        return;
    }

    // --- Global keyboard shortcuts — always active, regardless of mode ---
    // Note: plain Tab is NOT global — widgets receive it first (e.g. an editor
    // inserts a tab, or a file manager switches panels). If a widget returns
    // Ignored, nothing happens (plain Tab no longer switches windows).
    // Global window switching is EXCLUSIVELY Alt+Tab (a bare Linux console sends
    // this as ESC+Tab — the only modified combo available on a TTY; Ctrl+Tab
    // emits NO bytes with the default keymap, and keymaps with a separate
    // sequence do not switch windows either).
    if (ev.key == Key::Tab && ev.alt) { wm_.focusNext(); return; }
    if (ev.key == Key::F1) { openAppMenu(); return; }
    if (ev.key == Key::F9) { openNotificationCenter(); return; }
    if (ev.key == Key::F10) { openBackgroundTasks(); return; }
    if (ev.ctrl && ev.key == Key::Char && ev.character == U'w') {
        // Guard: a dirty window (Widget::dirty) prompts for confirmation before closing.
        Window* w = wm_.focused();
        closeWithConfirm(*this, w, [this, w] { closePending_.push_back(w); });
        return;
    }
    if (ev.key == Key::F5 && wm_.focused()) { mode_ = CursorMode::WindowMove; return; }
    if (ev.key == Key::F6 && wm_.focused()) { mode_ = CursorMode::WindowResize; return; }
    if (ev.key == Key::F7 && wm_.focused()) { toggleMaximize(*wm_.focused()); return; }
    if (ev.key == Key::F8 && wm_.focused()) { wm_.focused()->setMinimized(true); return; }

    // --- WindowMove / WindowResize (handled first so Esc exits immediately) ---
    if (mode_ == CursorMode::WindowMove || mode_ == CursorMode::WindowResize) {
        Window* f = wm_.focused();
        if (!f) { mode_ = CursorMode::Desktop; return; }
        if (ev.key == Key::Enter || ev.key == Key::Escape) {
            mode_ = CursorMode::Desktop;
            cursor_.moveTo(f->bounds().x + 2, f->bounds().y);
            cursor_.clamp(screenWidth(), screenHeight());
            return;
        }
        bool moving = (mode_ == CursorMode::WindowMove);
        switch (ev.key) {
            case Key::Up:    moving ? f->moveBy(0, -1) : f->resizeBy(0, -1); break;
            case Key::Down:  moving ? f->moveBy(0, 1)  : f->resizeBy(0, 1);  break;
            case Key::Left:  moving ? f->moveBy(-1, 0) : f->resizeBy(-1, 0); break;
            case Key::Right: moving ? f->moveBy(1, 0)  : f->resizeBy(1, 0);  break;
            default: break;
        }
        f->constrainTo(desktopArea());
        return;
    }

    // --- Edit mode — keys go directly to the editable widget ---
    if (mode_ == CursorMode::Edit) {
        Window* f = wm_.focused();
        if (!f || f->minimized()) { mode_ = CursorMode::Desktop; return; }

        // F2 opens the application menu (also works in Edit mode).
        if (ev.key == Key::F2) {
            if (f->content() && !f->content()->menuTabs().empty()) openWinMenu(*f, 0);
            return;
        }
        auto* content = f->content();
        // Check application shortcuts BEFORE the widget (e.g. Ctrl+S in TextEditor).
        if (content && matchesShortcuts(content->shortcuts(), ev)) {
            invokeShortcut(content->shortcuts(), ev);
            return;
        }
        // The widget gets the key FIRST: it may consume Escape (cancel a dialog)
        // or Tab (switch a file-manager panel). applyWidgetResult handles the
        // Escape fallback when the widget returns Ignored.
        applyWidgetResult(f->handleKey(ev), ev);
        return;
    }

    // --- Keyboard mode — navigate widgets in the focused window ---
    if (mode_ == CursorMode::Keyboard) {
        Window* f = wm_.focused();
        if (!f || f->minimized()) { mode_ = CursorMode::Desktop; return; }

        // F2 opens or switches to the application's menu (first tab).
        if (ev.key == Key::F2) {
            auto& tabs = f->content()->menuTabs();
            if (!tabs.empty()) openWinMenu(*f, 0);
            return;
        }

        // Check application shortcuts BEFORE the widget.
        auto* content = f->content();
        if (content && matchesShortcuts(content->shortcuts(), ev)) {
            invokeShortcut(content->shortcuts(), ev);
            return;
        }

        // A character enters Edit mode for editable widgets
        // (ignore Alt+character; it is not text input).
        if (ev.key == Key::Char && !ev.ctrl && !ev.alt && content && content->editable()) {
            mode_ = CursorMode::Edit;
        }

        // Delegate to the content widget; Ignored + Esc uses the global fallback
        // (Esc -> desktop; plain Tab deliberately does nothing — see applyWidgetResult).
        // A widget can also request Edit mode by returning EnteredEdit (e.g. LineEdit).
        applyWidgetResult(f->handleKey(ev), ev);
        return;
    }

    // --- Desktop mode — free cursor movement across the screen ---
    if (ev.key == Key::F2) {
        // Open the menu of the first window whose title bar contains the cursor;
        // otherwise, open the focused window's menu.
        Window* under = wm_.windowAt(cursor_.x(), cursor_.y());
        Window* target = (under && !under->minimized() && under->content() &&
                          under->content()->menuTabs().size() > 0 &&
                          (under->onTitleBar(cursor_.x(), cursor_.y()) ||
                           under->onMenuRow(cursor_.x(), cursor_.y())))
                             ? under
                             : wm_.focused();
        if (target && target->content() && !target->content()->menuTabs().empty())
            openWinMenu(*target, 0);
        return;
    }

    // Plain Tab deliberately does NOT switch windows on the desktop — the only
    // ways to switch are Alt+Tab (global; see above) and the taskbar (Enter on a
    // window handle).

    // --- Taskbar — window navigation and activation ------------------
    if (cursor_.y() >= screenHeight() - kTaskBarHeight) {
        const auto& wins = wm_.windows();
        int totalHandles = static_cast<int>(wins.size());
        if (totalHandles == 0) {
            // Empty taskbar (no windows open): Down does not move lower, Up
            // returns to the desktop, Left/Right can still move along the bar,
            // and Enter/Space activates nothing. IMPORTANT: do NOT teleport the
            // cursor to (0, bottom) — it would get stuck in the lower-left corner
            // (this branch runs BEFORE handling the Up arrow).
            if (ev.key == Key::Up) {
                cursor_.moveTo(cursor_.x(), screenHeight() - kTaskBarHeight - 1);
            } else if (ev.key == Key::Left || ev.key == Key::Right) {
                cursor_.moveBy(ev.key == Key::Left ? -1 : 1, 0);
                cursor_.clamp(screenWidth(), screenHeight());
            }
            return;
        }

        int canScrollLeft  = taskbarScroll_ > 0;
        int rightArrowWidth = 1;
        int usableWidth    = screenWidth() - (canScrollLeft ? 1 : 0) - rightArrowWidth;
        int maxVisible     = std::max(1, usableWidth / kTaskHandleW);

        if (ev.key == Key::Left) {
            // Move left one position; if scrolling is possible and we are at the start, scroll left.
            if (canScrollLeft && cursor_.x() <= kTaskHandleW) {
                taskbarScroll_ = std::max(0, taskbarScroll_ - 1);
            } else {
                cursor_.moveBy(-1, 0);
            }
            return;
        }
        if (ev.key == Key::Right) {
            // Move right one position; if scrolling is possible and we are at the end, scroll right.
            int arrowX = screenWidth() - 1;
            bool canScrollRight = (taskbarScroll_ + maxVisible < totalHandles);
            if (canScrollRight && cursor_.x() >= arrowX - kTaskHandleW + 1) {
                taskbarScroll_ += 1;
            } else {
                cursor_.moveBy(1, 0);
            }
            return;
        }
        if (ev.key == Key::Enter || (ev.key == Key::Char && !ev.ctrl && ev.character == U' ')) {
            // Activate the window under the cursor on the taskbar.
            int canScrollL = taskbarScroll_ > 0;
            usableWidth    = screenWidth() - (canScrollL ? 1 : 0) - rightArrowWidth;
            maxVisible     = std::max(1, usableWidth / kTaskHandleW);

            int visualStartX = canScrollL ? 1 : 0; // position of the first visible handle
            int winIdx       = (cursor_.x() - visualStartX) / kTaskHandleW;
            winIdx += taskbarScroll_;             // add the scroll offset

            if (winIdx >= 0 && winIdx < totalHandles) {
                Window* w = wins[static_cast<std::size_t>(winIdx)].get();
                wm_.focus(w);                       // focus() restores minimized windows
                mode_ = enterWindowMode(*w);         // enter window mode
            }
            return;
        }
        // If the cursor is on the taskbar and the user presses Up, leave the
        // taskbar and return to the desktop.
        if (ev.key == Key::Up) {
            cursor_.moveTo(cursor_.x(), screenHeight() - 2);
            return;
        }

        // Down does nothing (the cursor is already on the taskbar).
        // Ignore all other keys (mainly Enter/Space, which are handled above).
        return;
    }

    switch (ev.key) {
        case Key::Up: cursor_.moveBy(0, -1); break;
        case Key::Down: cursor_.moveBy(0, 1); break;
        case Key::Left: cursor_.moveBy(-1, 0); break;
        case Key::Right: cursor_.moveBy(1, 0); break;
        case Key::Enter: activateAtCursor(); break;
        case Key::Char: if (!ev.ctrl && ev.character == U' ') activateAtCursor(); break;
        default: break;
    }
    cursor_.clamp(screenWidth(), screenHeight());
}

void Desktop::drawDesktopBackground(Screen& screen) {
    const auto& t = theme::active;
    const auto& wp = settings::active.wallpaper;
    // Disabled wallpaper = the classic theme-colored fill. Enabled solid =
    // the chosen background color. Enabled pattern = the chosen glyph
    // repeated in the pattern color on the background color.
    const bool pattern = wp.enabled && wp.mode == wallpaper::Wallpaper::Mode::Pattern;
    const char32_t ch = pattern ? wp.glyph : U' ';
    const Color fg = pattern ? Color{static_cast<uint8_t>(wp.fg & 15)} : t.desktopText;
    const Color bg = wp.enabled ? Color{static_cast<uint8_t>(wp.bg & 15)} : t.desktopBg;
    for (int y = kMenuBarHeight; y < screen.height() - kTaskBarHeight; ++y)
        for (int x = 0; x < screen.width(); ++x)
            screen.set(x, y, Cell{ch, fg, bg, 0});
}

void Desktop::drawMenuBar(Screen& screen) {
    const auto& t = theme::active;
    for (int x = 0; x < screen.width(); ++x)
        screen.set(x, 0, Cell{U' ', t.menuBarText, t.menuBarBg, 0});

    std::u32string left = U" Applications ";
    screen.putText(0, 0, left, t.menuBarText, t.menuBarBg, StyleBold);

    // Mode in the upper-right corner: [mode] | Fenrir — where "Fenrir" used to be.
    std::u32string modeLabel = U"Desktop";
    switch (mode_) {
        case CursorMode::Keyboard:  modeLabel = U"Keyboard"; break;
        case CursorMode::Edit:      modeLabel = U"Edit"; break;
        case CursorMode::WindowMove:   modeLabel = U"Move"; break;
        case CursorMode::WindowResize: modeLabel = U"Resize"; break;
        default: break;
    }
    // Clock settings: taskbarSeconds adds ":SS" at the end.
    // The desktop name also comes from settings (changes in Settings appear
    // immediately because drawMenuBar reads the settings on each draw).
    const std::string& name =
        settings::active.desktopName.empty() ? desktopName_ : settings::active.desktopName;
    std::u32string right = modeLabel + U" | " + text::toUtf32(name);
    const int unread = notifications::global().unreadCount();
    if (unread > 0) right = text::toUtf32(std::to_string(unread)) + U" notif | " + right;
    if (!backgroundWorkers().empty())
        right = text::toUtf32(std::to_string(backgroundWorkers().size())) + U" bg | " + right;
    int rightX = screen.width() - static_cast<int>(right.size()) - 1;
    if (rightX > static_cast<int>(left.size()))
        screen.putText(rightX, 0, right, t.menuBarText, t.menuBarBg, StyleBold);
}

void Desktop::drawTaskBar(Screen& screen) {
    // Themed taskbar: the active handle has its own foreground/background pair;
    // clock and scroll arrows use the theme accent.
    const auto& t = theme::active;
    const Color barBg = t.taskbarBg;
    const Color activeFg = t.taskbarActiveText;
    const Color activeBg = t.taskbarActiveBg;
    const Color inactiveFg = t.taskbarText;
    const Color accent = t.highlightText;

    int y = screen.height() - 1;
    for (int x = 0; x < screen.width(); ++x)
        screen.set(x, y, Cell{' ', inactiveFg, barBg, 0});

    const auto& wins = wm_.windows();
    int totalHandles = static_cast<int>(wins.size());

    // Compute scroll state (needed even for clock layout)
    bool canScrollLeft  = taskbarScroll_ > 0;
    bool canScrollRight = true;
    int leftArrowWidth  = canScrollLeft  ? 1 : 0;
    int rightArrowWidth = 1;

    if (totalHandles > 0) {
        int usableWidth    = screen.width() - leftArrowWidth - rightArrowWidth;
        int maxVisible     = std::max(1, usableWidth / kTaskHandleW);

        if (taskbarScroll_ + maxVisible > totalHandles)
            taskbarScroll_ = std::max(0, totalHandles - maxVisible);
        canScrollRight = (taskbarScroll_ + maxVisible < totalHandles);

        const Window* activeWin = wm_.focused();
        int visibleStart = taskbarScroll_;
        int visibleEnd   = std::min(visibleStart + maxVisible, totalHandles);

        for (int idx = visibleStart; idx < visibleEnd; ++idx) {
            Window* w = wins[static_cast<std::size_t>(idx)].get();
            bool isMinimized = w->minimized();
            bool isActive    = (w == activeWin && !isMinimized);

            Color bg = isActive ? activeBg : barBg;
            Color fg = isActive ? activeFg : inactiveFg;

            // Build title label with ASCII-only [ ] brackets and ... for truncation
            std::string latin(w->title());
            int maxChars = kTaskHandleW - 4;  // leave room for [ ] and ...
            if (static_cast<int>(latin.size()) > maxChars) {
                latin.resize(static_cast<std::size_t>(maxChars));
            }
            std::string label = "[" + latin + "]";
            int pad = kTaskHandleW - static_cast<int>(label.size());
            if (pad < 0) pad = 1; // minimum padding
            label.insert(1, static_cast<std::size_t>(pad / 2), ' ');

            int x = leftArrowWidth + (idx - visibleStart) * kTaskHandleW;
            if (x + static_cast<int>(label.size()) < screen.width()) {
                screen.putText(x, y, text::toUtf32(label), fg, bg, isActive ? StyleBold : StyleNone);
            }
        }

        // ASCII scroll arrows
        if (canScrollLeft) screen.set(0, y, Cell{'<', accent, barBg});
        if (canScrollRight) screen.set(screen.width() - 1, y, Cell{'>', accent, barBg});
    }

    // --- Date/time: ALWAYS in the same place (right edge, beside the > arrow). ---
    // The clock area is reserved: window handles never overlap it. The clock
    // NEVER shifts when windows are opened.
    time_t now = time(nullptr);
    struct tm *ti = localtime(&now);
    if (ti) {
        // taskbar_seconds=1 includes seconds; otherwise, use the usual format.
        char tbuf[24];
        const char* fmt = settings::active.taskbarSeconds ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M";
        strftime(tbuf, sizeof(tbuf), fmt, ti);
        std::string clockStr(tbuf);
        int clockX = screen.width() - 1 - static_cast<int>(clockStr.size()); // right-aligned
        if (clockX < leftArrowWidth) clockX = leftArrowWidth;
        if (clockX + static_cast<int>(clockStr.size()) <= screen.width()) {
            screen.putText(clockX, y, std::u32string(clockStr.begin(), clockStr.end()), accent, barBg);
        }
    }
}

void Desktop::positionGuiCursor() {
    // In Edit and Keyboard modes, the GUI cursor follows the widget's internal
    // position (e.g. the paint cursor or selected palette cell in Paint).
    // In Keyboard mode the cursor used to sit in the window; now its blink shows
    // what is being edited or selected. Desktop mode keeps the free pointer.
    bool widgetWantsCursor = true;
    if (mode_ == CursorMode::Edit || mode_ == CursorMode::Keyboard) {
        Window* f = wm_.focused();
        Widget* content = (f && !f->minimized()) ? f->content() : nullptr;
        if (content) {
            widgetWantsCursor = content->wantsGuiCursor();
            auto pos = content->cursorPosition();
            if (pos) cursor_.moveTo(pos->first, pos->second);
        }
    }
    // In Desktop/WindowMove/Resize mode, the cursor stays where it was last placed.
    cursor_.setVisible(cursorBlinkState_ && widgetWantsCursor);
    cursor_.render(screen_);
}

void Desktop::addFdSource(int fd, std::function<void()> onReady,
                          std::function<bool()> wantsWrite) {
    if (fd < 0 || !onReady) return;
    fdSources_.push_back(FdSource{fd, std::move(onReady), std::move(wantsWrite)});
}

void Desktop::removeFdSource(int fd) {
    std::erase_if(fdSources_, [fd](const FdSource& source) { return source.fd == fd; });
}

void Desktop::tickOnce() {
    if (g_shutdownRequested.load()) {
        g_shutdownRequested.store(false);
        performShutdown();
        running_ = false;
        return;
    }

    const auto saveNow = std::chrono::steady_clock::now();
    if (lastSave_ == std::chrono::steady_clock::time_point{} ||
        saveNow - lastSave_ >= std::chrono::seconds(30)) {
        performShutdown();
    }

    // Asynchronous sources (AppContext::addFdSource, e.g. terminal PTY):
    // poll without blocking. A callback may read data and remove its source,
    // so iterate over a copy of the vector.
    auto sources = fdSources_;

    // Background workers (registered by apps) run every frame, even when the
    // app has no open window — e.g. calendar event notifications.
    auto workers = backgroundWorkers_;
    for (auto& w : workers) {
        if (w.tick) w.tick(*this);
    }
    for (const auto& source : sources) {
        short events = POLLIN;
        if (source.wantsWrite && source.wantsWrite()) events |= POLLOUT;
        struct pollfd pfd { source.fd, events, 0 };
        if (poll(&pfd, 1, 0) > 0 &&
            (pfd.revents & (events | POLLHUP | POLLERR | POLLNVAL)) && source.onReady)
            source.onReady();
    }

    char buf[64];
    // With registered sources (terminal window), use a shorter interval so
    // child-process output appears smoothly; without sources, use the usual 400ms.
    const int timeout = fdSources_.empty() ? 400 : 30;
    std::size_t n = terminal_.read(buf, sizeof(buf), timeout);

    if (terminal_.consumeResizeFlag()) {
        auto sz = terminal_.size();
        screen_.resize(sz.columns, sz.rows);
        for (const auto& window : wm_.windows()) window->constrainTo(desktopArea());
        renderer_.forceFullRedraw();
        cursor_.clamp(screenWidth(), screenHeight());
    }

    // Cursor blinking uses steady_clock, not a tick counter —
    // a tick with the terminal open takes ~30ms (fd sources) instead of ~400ms,
    // so per-tick blinking would be more than ten times too fast.
    const auto now = std::chrono::steady_clock::now();
    if (n > 0) {
        cursorBlinkState_ = true; // Keep the cursor visible while typing.
        lastBlinkToggle_ = now;
        for (const KeyEvent& ev : keyParser_.feed(buf, n)) handleKey(ev);
        // A read ending mid-sequence (split Alt+key) holds its tail in the
        // parser: peek briefly for the rest instead of stalling Escape until
        // the next tick. 30 ms is imperceptible for a lone Escape and an
        // eternity for transport-split bytes, which arrive back-to-back.
        while (keyParser_.wantsMore()) {
            const std::size_t m = terminal_.read(buf, sizeof(buf), 30);
            if (m == 0) {
                for (const KeyEvent& ev : keyParser_.flush()) handleKey(ev);
            } else {
                for (const KeyEvent& ev : keyParser_.feed(buf, m)) handleKey(ev);
            }
        }
    } else {
        // Read timed out: no more bytes are coming imminently, so any escape
        // sequence the parser held back for reassembly stands alone (a lone
        // Escape, or a truncated sequence). Drain it now rather than leaving
        // a pressed Escape unhandled until the next keypress.
        for (const KeyEvent& ev : keyParser_.flush()) handleKey(ev);
    }
    if (settings::active.cursorBlink) {
        // cursor_blink=0 in settings makes the cursor static.
        constexpr auto kBlinkInterval = std::chrono::milliseconds(400);
        if (now - lastBlinkToggle_ >= kBlinkInterval) {
            cursorBlinkState_ = !cursorBlinkState_;
            lastBlinkToggle_ = now;
        }
    } else {
        cursorBlinkState_ = true;
        lastBlinkToggle_ = now;
    }

    // === DRAW ORDER (DO NOT REORDER — each layer overlaps the one before): ===
    // 1. Desktop background        2. Windows                     3. Menu bar (top row)
    // 4. Taskbar (bottom row)     5. App dropdown (F1)           6. Win menu dropdown
    // 7. GUI cursor (fg/bg swap — MUST be last to stay on top) ===

    screen_.clear(Cell{U' ', colors::White, colors::Black, 0});
    drawDesktopBackground(screen_);
    wm_.draw(screen_);      // windows (below the bars)
    drawMenuBar(screen_);   // menu bar on top of windows
    drawTaskBar(screen_);   // taskbar on top of everything

    if (appMenu_) {
        drawBox(screen_, appMenuBounds_, U"Applications");
        appMenu_->draw(screen_);
    }

    // Per-application menu — dropdown below the window title bar.
    if (winMenu_) {
        drawBox(screen_, winMenuBounds_, winMenuTitle_);
        winMenu_->draw(screen_);
    }

    positionGuiCursor();
    renderer_.present(screen_);
}

int Desktop::run() {
    ::signal(SIGINT, fenrirSignalHandler);
    ::signal(SIGTERM, fenrirSignalHandler);
    ::signal(SIGHUP, fenrirSignalHandler);

    notifications::global().load();
    for (auto& a : registry_.apps()) a->onStartup(*this);
    if (notifications::global().unreadCount() > 0) {
        missedNotificationsShown_ = true;
        openNotificationCenter();
    }
    lastSave_ = std::chrono::steady_clock::now();
    renderer_.forceFullRedraw();
    while (running_) tickOnce();
    performShutdown();
    return 0;
}

} // namespace fenrir
