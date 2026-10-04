#include "fenrir/desktop/Desktop.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

#include "fenrir/core/FileSystem.hpp"

namespace fenrir {

namespace {

void drawBox(Screen& screen, const Rect& r, std::u32string_view title) {
    const Color border = colors::Yellow;
    const Color bg = colors::Black;

    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            screen.set(x, y, Cell{U' ', border, bg, 0});

    screen.set(r.x, r.y, Cell{U'┌', border, bg, 0});
    screen.set(r.x + r.w - 1, r.y, Cell{U'┐', border, bg, 0});
    screen.set(r.x, r.y + r.h - 1, Cell{U'└', border, bg, 0});
    screen.set(r.x + r.w - 1, r.y + r.h - 1, Cell{U'┘', border, bg, 0});
    for (int x = r.x + 1; x < r.x + r.w - 1; ++x) {
        screen.set(x, r.y, Cell{U'─', border, bg, 0});
        screen.set(x, r.y + r.h - 1, Cell{U'─', border, bg, 0});
    }
    for (int y = r.y + 1; y < r.y + r.h - 1; ++y) {
        screen.set(r.x, y, Cell{U'│', border, bg, 0});
        screen.set(r.x + r.w - 1, y, Cell{U'│', border, bg, 0});
    }
    if (!title.empty()) {
        std::u32string t = U" ";
        t += title;
        t += U" ";
        screen.putText(r.x + 2, r.y, t, bg, border, StyleBold);
    }
}

} // namespace

Desktop::Desktop()
    : terminal_(),
      screen_(terminal_.size().columns, terminal_.size().rows),
      renderer_(terminal_),
      wm_(),
      registry_() {}

Rect Desktop::desktopArea() const {
    int h = std::max(0, screenHeight() - kMenuBarHeight - kStatusBarHeight);
    return Rect{0, kMenuBarHeight, screenWidth(), h};
}

void Desktop::openAppMenu() {
    if (appMenu_) return;

    std::vector<std::u32string> items;
    for (auto& a : registry_.apps()) {
        std::string n = a->name();
        items.emplace_back(n.begin(), n.end());
    }
    items.push_back(U"Zamknij pulpit");

    int w = 24;
    for (auto& s : items) w = std::max<int>(w, static_cast<int>(s.size()) + 4);
    int h = static_cast<int>(items.size()) + 2;
    appMenuBounds_ = Rect{1, kMenuBarHeight, w, h};

    auto menu = std::make_unique<Menu>(items);
    Menu* raw = menu.get();
    raw->setBounds(Rect{appMenuBounds_.x + 1, appMenuBounds_.y + 1, appMenuBounds_.w - 2, appMenuBounds_.h - 2});
    appMenu_ = std::move(menu);

    raw->onSelect = [this](int index) {
        auto& list = registry_.apps();
        if (index >= 0 && static_cast<std::size_t>(index) < list.size()) {
            launchApp(*list[static_cast<std::size_t>(index)]);
        } else {
            quit();
        }
        closeAppMenu();
    };
}

void Desktop::closeAppMenu() { appMenu_.reset(); }

void Desktop::launchApp(App& app) {
    Rect area = desktopArea();
    Rect bounds = app.preferredBounds(area);
    Window* w = wm_.createWindow(app.name(), bounds, app.createView(*this));
    wm_.focus(w);
    cursor_.moveTo(w->bounds().x + 2, w->bounds().y + 1);
    cursor_.clamp(screenWidth(), screenHeight());
    // Od razu w tryb Interact - po wybraniu aplikacji z menu mozna zaczac
    // pisac/nawigowac bez dodatkowego Enter na zawartosci okna.
    mode_ = CursorMode::Interact;
}

void Desktop::openDialogWindow(FileDialogMode mode, const std::string& startDir,
                                const std::string& suggestedName, OnPick onPick, OnCancel onCancel) {
    std::string dir = fs::isDirectory(startDir) ? startDir : fs::parent(startDir);
    if (!fs::isDirectory(dir)) dir = "/";

    // Okno, ktore poprosilo o dialog (np. edytor) - po zamknieciu dialogu
    // wracamy WLASNIE do niego w trybie Interact, zeby uzytkownik mogl od
    // razu kontynuowac prace. Bez tego tryb spadal do Desktop i kolejne
    // klawisze (np. dalsze pisanie) nie trafialy juz do widgetu.
    Window* requester = wm_.focused();

    auto dialog = std::make_unique<FileDialog>(mode, dir, suggestedName);
    FileDialog* raw = dialog.get();

    Rect area = desktopArea();
    Rect bounds{area.x + 2, area.y + 1, std::min(56, std::max(24, area.w - 4)),
                std::min(16, std::max(8, area.h - 2))};
    std::string title = mode == FileDialogMode::SaveFile  ? "Zapisz jako"
                         : mode == FileDialogMode::OpenFolder ? "Wybierz folder"
                                                               : "Otworz plik";
    Window* w = wm_.createWindow(title, bounds, std::move(dialog));
    wm_.focus(w);
    mode_ = CursorMode::Interact;

    // KRYTYCZNE: onConfirm/onCancel sa wolane Z WNETRZA FileDialog::handleKey,
    // czyli z metody obiektu, ktory wm_.closeWindow(w) zaraz zniszczy - a
    // zniszczy TEZ pamiec, w ktorej zyje TA WLASNIE lambda (onConfirm jest
    // skladowa std::function samego FileDialog). Dlatego closeWindow(w) MUSI
    // byc ostatnia instrukcja: wszystko, co czyta zlapane zmienne (this,
    // onPick/onCancel), musi wykonac sie PRZED nim. Odwrotna kolejnosc
    // (closeWindow na poczatku) powodowala realny SIGSEGV - zlapane w
    // testach pty, nie teoretycznie.
    raw->onConfirm = [this, w, requester, onPick = std::move(onPick)](const std::string& path) {
        if (onPick) onPick(path);
        if (requester) { wm_.focus(requester); mode_ = CursorMode::Interact; }
        else mode_ = CursorMode::Desktop;
        wm_.closeWindow(w);
    };
    raw->onCancel = [this, w, requester, onCancel = std::move(onCancel)]() {
        if (onCancel) onCancel();
        if (requester) { wm_.focus(requester); mode_ = CursorMode::Interact; }
        else mode_ = CursorMode::Desktop;
        wm_.closeWindow(w);
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
    openDialogWindow(FileDialogMode::SaveFile, startDir, suggestedName, std::move(onPick), std::move(onCancel));
}

void Desktop::toggleMaximize(Window& w) {
    if (w.maximized()) w.restore();
    else w.maximize(desktopArea());
}

void Desktop::activateAtCursor() {
    if (cursor_.y() < kMenuBarHeight) { openAppMenu(); return; }

    if (cursor_.y() >= screenHeight() - kStatusBarHeight) return; // pasek stanu - informacyjny

    Window* w = wm_.windowAt(cursor_.x(), cursor_.y());
    if (!w) return; // pusty pulpit - nic pod kursorem

    if (w->minimized()) { wm_.focus(w); return; } // focus() przywraca (patrz WindowManager::focus)

    wm_.focus(w);
    switch (w->titleBarZoneAt(cursor_.x(), cursor_.y())) {
        case TitleBarZone::CloseButton: wm_.closeWindow(w); break;
        case TitleBarZone::MinimizeButton: w->setMinimized(true); break;
        case TitleBarZone::MaximizeButton: toggleMaximize(*w); break;
        case TitleBarZone::Drag: mode_ = CursorMode::WindowMove; break;
        case TitleBarZone::None: mode_ = CursorMode::Interact; break;
    }
}

void Desktop::handleKey(const KeyEvent& ev) {
    // Ctrl+Q dziala ZAWSZE, niezaleznie od trybu kursora/menu - to "wylacznik
    // awaryjny". Bez tego dałoby się utknac np. w trybie WindowMove bez
    // mozliwosci wyjscia z aplikacji.
    if (ev.ctrl && ev.key == Key::Char && ev.character == U'q') { quit(); return; }

    if (appMenu_) {
        if (ev.key == Key::Escape) { closeAppMenu(); return; }
        appMenu_->handleKey(ev);
        return;
    }

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
        return;
    }

    if (mode_ == CursorMode::Interact) {
        if (ev.key == Key::Escape) { mode_ = CursorMode::Desktop; return; }
        if (ev.ctrl && ev.key == Key::Char && ev.character == U'w') {
            wm_.closeFocused();
            mode_ = CursorMode::Desktop;
            return;
        }
        if (Window* f = wm_.focused()) f->handleKey(ev);
        return;
    }

    // mode_ == CursorMode::Desktop
    if (ev.ctrl && ev.key == Key::Char && ev.character == U'w') { wm_.closeFocused(); return; }
    if (ev.key == Key::Tab) { wm_.focusNext(); return; }
    if (ev.key == Key::F1) { openAppMenu(); return; }
    if (ev.key == Key::F5 && wm_.focused()) { mode_ = CursorMode::WindowMove; return; }
    if (ev.key == Key::F6 && wm_.focused()) { mode_ = CursorMode::WindowResize; return; }
    if (ev.key == Key::F7 && wm_.focused()) { toggleMaximize(*wm_.focused()); return; }
    if (ev.key == Key::F8 && wm_.focused()) { wm_.focused()->setMinimized(true); return; }

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
    for (int y = kMenuBarHeight; y < screen.height() - kStatusBarHeight; ++y)
        for (int x = 0; x < screen.width(); ++x)
            screen.set(x, y, Cell{U' ', colors::White, colors::DarkBlue, 0});
}

void Desktop::drawMenuBar(Screen& screen) {
    for (int x = 0; x < screen.width(); ++x)
        screen.set(x, 0, Cell{U' ', colors::White, colors::Blue, 0});

    std::u32string left = U" Aplikacje (F1) ";
    screen.putText(0, 0, left, colors::White, colors::Blue, StyleBold);

    std::u32string name(desktopName_.begin(), desktopName_.end());
    int rightX = screen.width() - static_cast<int>(name.size()) - 1;
    if (rightX > static_cast<int>(left.size()))
        screen.putText(rightX, 0, name, colors::White, colors::Blue, StyleBold);
}

void Desktop::drawStatusBar(Screen& screen) {
    int y = screen.height() - 1;
    for (int x = 0; x < screen.width(); ++x)
        screen.set(x, y, Cell{U' ', colors::Black, colors::Gray, 0});

    std::u32string hints =
        U"F1 menu | strzalki: kursor/okno | Enter aktywuj | F5/F6 przesun/rozmiar | F7 maks | F8 min | Ctrl+W zamknij | Ctrl+Q wyjscie";
    if (static_cast<int>(hints.size()) > screen.width() - 2)
        hints.resize(static_cast<std::size_t>(std::max(0, screen.width() - 2)));
    screen.putText(1, y, hints, colors::Black, colors::Gray);

    std::u32string list;
    for (auto& w : wm_.windows()) {
        std::string t = w->title();
        if (!list.empty()) list += U" | ";
        if (w->minimized()) list += U"[_]";
        list += std::u32string(t.begin(), t.end());
    }
    int listX = screen.width() - static_cast<int>(list.size()) - 1;
    int minX = 2 + static_cast<int>(hints.size());
    if (listX > minX) screen.putText(listX, y, list, colors::Black, colors::Gray, StyleBold);
}

void Desktop::positionGuiCursor() {
    if (mode_ == CursorMode::Interact) {
        Window* f = wm_.focused();
        auto pos = (f && !f->minimized()) ? f->content()->cursorPosition() : std::nullopt;
        if (pos) cursor_.moveTo(pos->first, pos->second);
    }
    cursor_.setVisible(cursorBlinkState_);
    cursor_.render(screen_);
}

void Desktop::tickOnce() {
    char buf[64];
    std::size_t n = terminal_.read(buf, sizeof(buf), 400 /*ms - tempo migania kursora*/);

    if (terminal_.consumeResizeFlag()) {
        auto sz = terminal_.size();
        screen_.resize(sz.columns, sz.rows);
        renderer_.forceFullRedraw();
        cursor_.clamp(screenWidth(), screenHeight());
    }

    if (n == 0) {
        cursorBlinkState_ = !cursorBlinkState_;
    } else {
        cursorBlinkState_ = true;
        for (const KeyEvent& ev : keyParser_.feed(buf, n)) handleKey(ev);
    }

    screen_.clear(Cell{U' ', colors::White, colors::Black, 0});
    drawDesktopBackground(screen_);
    wm_.draw(screen_);
    drawMenuBar(screen_);
    drawStatusBar(screen_);

    if (appMenu_) {
        drawBox(screen_, appMenuBounds_, U"Aplikacje");
        appMenu_->draw(screen_);
    }

    positionGuiCursor();
    renderer_.present(screen_);
}

int Desktop::run() {
    renderer_.forceFullRedraw();
    while (running_) tickOnce();
    return 0;
}

} // namespace fenrir
