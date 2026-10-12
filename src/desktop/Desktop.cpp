#include "fenrir/desktop/Desktop.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdio>
#include <string>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Settings.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/window/Window.hpp"

namespace fenrir {

namespace {

void drawBox(Screen& screen, const Rect& r, std::u32string_view title) {
    // Ramka dropdownu z motywu: niebieski pasek tytulu (titleBg/titleText),
    // szara ramka (menuBg/menuText), biale wnetrze (surfaceBg/surfaceText).
    const auto& t = theme::active;
    const Color titleBg = t.titleBg;
    const Color titleFg = t.titleText;
    const Color frameBg = t.menuBg;
    const Color frameFg = t.menuText;
    const Color innerFg = t.surfaceText;
    const Color innerBg = t.surfaceBg;

    // Wnętrze — białe (menu wypełni je wpisami; puste miejsca zostają białe).
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            screen.set(x, y, Cell{U' ', innerFg, innerBg, 0});

    // Górny wiersz = niebieski pasek tytułu.
    for (int x = r.x; x < r.x + r.w; ++x)
        screen.set(x, r.y, Cell{U' ', titleFg, titleBg, StyleBold});

    // Ramka boczna i dolna — szara.
    for (int y = r.y + 1; y < r.y + r.h; ++y) {
        screen.set(r.x, y, Cell{U'|', frameFg, frameBg, 0});
        screen.set(r.x + r.w - 1, y, Cell{U'|', frameFg, frameBg, 0});
    }
    for (int x = r.x; x < r.x + r.w; ++x)
        screen.set(x, r.y + r.h - 1, Cell{U' ', frameFg, frameBg, 0});

    // Nazwa na pasku tytułu.
    if (!title.empty()) {
        std::u32string t = U" " + std::u32string(title) + U" ";
        screen.putText(r.x + 1, r.y, t, titleFg, titleBg, StyleBold);
    }
}

} // namespace

// Pomocnicze funkcje do obslugi skrótow klawiszowych — statyczne, bo nie modyfikuja stanu Desktop.

bool Desktop::matchesShortcuts(const std::vector<std::pair<KeyShortcut, std::function<void()>>>& sc, const KeyEvent& ev) {
    for (auto& [shortcut, _] : sc) {
        if (shortcut.ctrl != ev.ctrl) continue;
        if (shortcut.key != ev.key) continue;
        // Dla Key::Char porownujemy character — uwaga: terminal wysyla lowercase!
        if (shortcut.key == Key::Char) {
            if (shortcut.character.has_value()) {
                char32_t sc = shortcut.character.value();
                // Dopasowanie case-insensitive dla liter (terminal zawsze wysyla lowercase).
                char32_t ec = ev.character;
                if (sc != ec && std::tolower(static_cast<unsigned char>(sc)) != std::tolower(static_cast<unsigned char>(ec))) continue;
            } else {
                // Bez zadanego character = niepasuje (nie wiemy co porownac).
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
    // Ustawienia trwale (~/.config/fenrir/settings.ini): motyw, nazwa,
    // zegar, migajacy kursor. Ladujemy przed pierwszym rysowaniem.
    settings::load();
    theme::apply(settings::active.theme);
    desktopName_ = settings::active.desktopName;
}

Rect Desktop::desktopArea() const {
    int h = std::max(0, screenHeight() - kMenuBarHeight - kTaskBarHeight);
    return Rect{0, kMenuBarHeight, screenWidth(), h};
}

void Desktop::openAppMenu() {
    if (appMenu_) return;
    closeWinMenu(); // menu aplikacji (dropdown) i menu pulpitowe nie istnieja razem
    appMenuClosePending_ = false; // czyscimy stale flagi z poprzedniego zamkniecia (np. po launchu aplikacji)

    std::vector<std::u32string> items;
    for (auto& a : registry_.apps()) {
        std::string n = a->name();
        items.emplace_back(n.begin(), n.end());
    }
    items.push_back(U"Zamknij pulpit");

    int w = 24;
    for (auto& s : items) w = std::max<int>(w, static_cast<int>(s.size()) + 4);
    int h = static_cast<int>(items.size()) + 2;
    appMenuBounds_ = Rect{1, kMenuBarHeight + 1, w, h}; // 1 wiersz odstepu od paska - na wysokosci 1szego wiersza okien

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
        // Domykamy PO powrocie z callbacku — reset() zniszczy Menu, wiec nie
        // mozemy go usunać z wewnatrz wlasnego onSelect (UB na std::function).
        appMenuClosePending_ = true;
    };
}

void Desktop::closeAppMenu() {
    appMenu_.reset();
    appMenuClosePending_ = false; // stale flaga po zamknieciu = domknie nastepne menu przy pierwszym klawiszu
}

// --- Menu konkretnej aplikacji (dropdown pod paskiem tytulu okna) -------------------

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

    appMenu_.reset(); // dropdown i menu pulpitowe (F1) wykluczaja sie wzajemnie

    // Zmiana karty w otwartym menu = przelaczenie na nowa karte.
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

    // Dropdown POD paskiem menu okna (wiersz menu = y+1), pod konkretna kartą.
    // Pobieramy pozycję karty z tabStripLayout().
    int itemW = 4;
    for (auto& l : labels) itemW = std::max<int>(itemW, static_cast<int>(l.size()) + 2);
    int h = static_cast<int>(labels.size()) + 2;
    int menuW = std::max(itemW, 12);

    Rect wb = w.bounds();
    auto strips = tabStripLayout(wb.x, wb.w, content->menuTabs());
    int x = strips.empty() ? wb.x : (strips[static_cast<std::size_t>(tab)].first + 1);
    int y = wb.y + 2; // pod paskiem tytulu (y+0) i paskiem menu (y+1)
    // Trzymamy dropdown w granicach ekranu.
    x = std::max(0, std::min(x, screenWidth() - menuW));
    y = std::max(kMenuBarHeight, std::min(y, screenHeight() - kTaskBarHeight - h));

    winMenuTitle_ = tabs[static_cast<std::size_t>(tab)].first;
    winMenuBounds_ = Rect{x, y, menuW, h};

    auto menu = std::make_unique<Menu>(std::move(labels));
    Menu* raw = menu.get();
    raw->setBounds(Rect{winMenuBounds_.x + 1, winMenuBounds_.y + 1,
                         winMenuBounds_.w - 2, winMenuBounds_.h - 2});
    winMenu_ = std::move(menu);

    // Snapshot: callback moze zamknac okno (np. pozycja "Wyjdziez"), wiec po
    // nim wskaznik winMenuWin_ moze byc dangling — trzymamy etykiety + callbacky
    // PO WARTOSCI w closure i nigdy nie wracamy po dane do okna.
    auto itemsCopy = items; // value-capture: std::function kopiuje safe
    raw->onSelect = [this, items = std::move(itemsCopy)](int index) {
        if (index >= 0 && static_cast<std::size_t>(index) < items.size() &&
            items[static_cast<std::size_t>(index)].cb) {
            items[static_cast<std::size_t>(index)].cb();
        }
        winMenuClosePending_ = true; // domkamy PO powrocie z callbacku (nie zniszczamy Menu w trakcie)
    };
}

// --- Dialogi: zamykanie okien PO powrocie z callbackow widgetow -------------------

void Desktop::flushPendingCloses() {
    if (closePending_.empty()) return;
    std::vector<Window*> pending = std::move(closePending_);
    closePending_.clear();
    for (Window* w : pending)
        if (windowAlive(w)) wm_.closeWindow(w);
}

void Desktop::restoreFocusTo(Window* requester) {
    if (requester && windowAlive(requester)) {
        wm_.focus(requester);
        mode_ = enterWindowMode(*requester);
    } else {
        // Wystawiajace okno zostalo zamkniete w miedzyczasie — nie dotykamy
        // wskaznika (dangling), po prostu wracamy na pulpit.
        mode_ = CursorMode::Desktop;
    }
}

void Desktop::applyWidgetResult(KeyEventResult r, const KeyEvent& ev) {
    if (r == KeyEventResult::EnteredEdit) { mode_ = CursorMode::Edit; return; }
    if (r != KeyEventResult::Ignored) return;
    // Widget nie chce tego klawisza — globalne fallbacki:
    // Tab = kolejne okno, Escape = cofniecie trybu (Edit -> Keyboard -> Desktop).
    if (ev.key == Key::Tab) { wm_.focusNext(); return; }
    if (ev.key == Key::Escape) {
        if (mode_ == CursorMode::Edit) mode_ = CursorMode::Keyboard;
        else leaveWindowMode();
    }
}

void Desktop::launchApp(App& app) {
    Rect area = desktopArea();
    Rect bounds = app.preferredBounds(area);
    Window* w = wm_.createWindow(app.name(), bounds, app.createView(*this));
    wm_.focus(w);
    cursor_.moveTo(w->bounds().x + 2, w->bounds().y + 1);
    cursor_.clamp(screenWidth(), screenHeight());
    // Wchodzimy w tryb Keyboard - uzytkownik nawiguje strzalami po widgetach
    // i aktywuje je Enterem/Space. Escape -> Desktop.
    mode_ = enterWindowMode(*w);
}

CursorMode Desktop::enterWindowMode(Window& w) {
    if (!w.content()) return CursorMode::Keyboard;
    return w.content()->editable() ? CursorMode::Edit : CursorMode::Keyboard;
}

void Desktop::leaveWindowMode() {
    // Usuwamy highlight ze wszystkich widgetow w fokusowanym oknie.
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
    std::string title = mode == FileDialogMode::SaveFile  ? "Zapisz jako"
                         : mode == FileDialogMode::OpenFolder ? "Wybierz folder"
                                                               : "Otworz plik";
    Window* w = wm_.createWindow(title, bounds, std::move(dialog));
    wm_.focus(w);
    mode_ = enterWindowMode(*w);

    // Zamkniecie odroczone (closePending_): callback jest wywolywany z wewnatrz
    // handleKey widgetu — nie mozemy zniszczyc okna na jego stosie wywolan.
    // Fokus wraca do wystawiajacego, ale TYLKO jesli jeszcze istnieje.
    raw->onConfirm = [this, w, requester, onPick = std::move(onPick)](const std::string& path) {
        if (onPick) onPick(path);
        // cb mogl otworzyc kolejny dialog (np. guardDirty -> openFile) — wtedy
        // nowe okno ma fokus i nie przywrocilismyby go wystawiajacemu, tylko
        // wrocilibysmy do starego zrodla, zostawiajac nowy dialog bez fokusa
        // (ignorowalby klawisze). Fokus cofamy tylko, gdy nikt go nie zabral.
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
    openDialogWindow(FileDialogMode::SaveFile, startDir, suggestedName, std::move(onPick), std::move(onCancel));
}

void Desktop::openDialog(std::u32string title, std::unique_ptr<Dialog> content, OnDialogResult onResult) {
    Window* requester = wm_.focused();
    Dialog* raw = content.get();

    // Rozmiar z samego dialogu (message wrap + pole + przyciski), wycentrowany.
    Rect area = desktopArea();
    int w = std::min(56, std::max(30, area.w - 6));
    int h = raw ? raw->heightForWidth(w) : 10;
    h = std::min(area.h - 2, std::max(5, h));
    Rect bounds{area.x + std::max(0, (area.w - w) / 2),
                area.y + std::max(0, (area.h - h) / 2), w, h};

    Window* win = wm_.createWindow(text::toUtf8(title), bounds, std::move(content));
    wm_.focus(win);
    mode_ = enterWindowMode(*win);

    // Callback widgetu: wynik -> app cb -> fokus -> OROCZONE zamkniecie okna
    // (widget zywy az do powrotu z handleKey; patrz flushPendingCloses).
    raw->onResult = [this, win, requester, cb = std::move(onResult)](int idx) {
        if (cb) cb(idx);
        // Ten sam wyjatek co w openDialogWindow: cb mogl otworzyc kolejny
        // dialog — wtedy to on ma fokus i nie wolamy restoreFocusTo.
        if (wm_.focused() == win) restoreFocusTo(requester);
        closePending_.push_back(win);
    };
}

void Desktop::toggleMaximize(Window& w) {
    if (w.maximized()) w.restore();
    else w.maximize(desktopArea());
}

void Desktop::activateAtCursor() {
    // Nie dotykamy appMenu_ (F1 menu) — to jest oddzielne, globalne menu.
    if (!appMenu_) {
        // Klik gdziekolwiek = domykaj otwarty dropdown menu aplikacji — ALE nie,
        // gdy klikamy w ta sama karte, ktora jest otwarta (bez migniecia).
        if (winMenu_ && winMenuWin_ && windowAlive(winMenuWin_)) {
            bool overSameTab = winMenuWin_->titleBarZoneAt(cursor_.x(), cursor_.y()) == TitleBarZone::MenuTab
                              && winMenuWin_->menuTabIndexAt(cursor_.x(), cursor_.y()) == winMenuTab_;
            if (!overSameTab) closeWinMenu();
        } else if (winMenu_) {
            closeWinMenu(); // okno juz nie zyla — czyścimy stan
        }
    }

    if (cursor_.y() < kMenuBarHeight) { openAppMenu(); return; }

    // --- Taskbar (status bar) — aktywacja uchwytu okna ----------------------
    if (cursor_.y() >= screenHeight() - kTaskBarHeight) {
        const auto& wins = wm_.windows();
        int totalHandles = static_cast<int>(wins.size());
        if (totalHandles == 0) return; // nic do aktywacji

        int canScrollLeft  = taskbarScroll_ > 0;
        int rightArrowWidth = 1;
        int usableWidth    = screenWidth() - (canScrollLeft ? 1 : 0) - rightArrowWidth;
        int maxVisible     = std::max(1, usableWidth / kTaskHandleW);

        // Sprawdź czy kliknięto na strzałkę scrolla
        if (cursor_.x() == 0 && canScrollLeft) {
            taskbarScroll_ = std::max(0, taskbarScroll_ - 1);
            return;
        }
        int arrowX = screenWidth() - 1;
        if (cursor_.x() == arrowX && (taskbarScroll_ + maxVisible < totalHandles)) {
            taskbarScroll_ += 1;
            return;
        }

        // Oblicz indeks okna pod kursorem
        int visualStartX = canScrollLeft ? 1 : 0;
        int winIdx       = (cursor_.x() - visualStartX) / kTaskHandleW;
        winIdx += taskbarScroll_;

        if (winIdx >= 0 && winIdx < totalHandles) {
            Window* w = wins[static_cast<std::size_t>(winIdx)].get();
            wm_.focus(w);                       // focus() przywraca zminimalizowane okna
            mode_ = enterWindowMode(*w);         // przejdź do trybu okna
        }
        return;
    }

    if (cursor_.y() >= screenHeight() - kTaskBarHeight) return; // task bar - informacyjny

    Window* w = wm_.windowAt(cursor_.x(), cursor_.y());
    if (!w) return; // pusty pulpit - nic pod kursorem

    if (w->minimized()) { wm_.focus(w); return; } // focus() przywraca

    wm_.focus(w);
    switch (w->titleBarZoneAt(cursor_.x(), cursor_.y())) {
        case TitleBarZone::CloseButton: wm_.closeWindow(w); break;
        case TitleBarZone::MinimizeButton: w->setMinimized(true); break;
        case TitleBarZone::MaximizeButton: toggleMaximize(*w); break;
        case TitleBarZone::Drag: mode_ = CursorMode::WindowMove; break;
        case TitleBarZone::MenuTab:
            openWinMenu(*w, w->menuTabIndexAt(cursor_.x(), cursor_.y()));
            break;
        case TitleBarZone::None: mode_ = enterWindowMode(*w); break;
    }
}

void Desktop::handleKey(const KeyEvent& ev) {
    // Wrapper: dispatch klawisza, a POTEM zamkniecie okien odroconych przez
    // callbacki dialogow (widget musi juz byc poza swoim handleKey).
    handleKeyEvent(ev);
    flushPendingCloses();
}

void Desktop::handleKeyEvent(const KeyEvent& ev) {
    // Ctrl+Q dziala ZAWSZE, niezaleznie od trybu kursora/menu - to "wylacznik
    // awaryjny". Bez tego dałoby się utknac np. w trybie WindowMove bez
    // mozliwosci wyjscia z aplikacji.
    if (ev.ctrl && ev.key == Key::Char && ev.character == U'q') { quit(); return; }

    // --- Menu pulpitowe (F1) ma najwiekszy priorytet ---
    if (appMenu_) {
        if (ev.key == Key::Escape) { closeAppMenu(); return; }
        appMenu_->handleKey(ev);
        if (appMenuClosePending_) closeAppMenu(); // PO powrocie z onSelect — bez UB
        return;
    }

    // --- Menu aplikacji (dropdown pod paskiem tytulu okna) ---
    if (winMenu_) {
        if (ev.key == Key::Escape) { closeWinMenu(); return; }
        Window* owner = winMenuWin_;
        auto nTabs = (owner && windowAlive(owner) && owner->content())
                         ? static_cast<int>(owner->content()->menuTabs().size())
                         : 0;
        // Left/Right = przelaczenie karty, o ile jest wiecej niz jedna.
        if (nTabs > 1) {
            if (ev.key == Key::Left)  { openWinMenu(*owner, (winMenuTab_ - 1 + nTabs) % nTabs); return; }
            if (ev.key == Key::Right) { openWinMenu(*owner, (winMenuTab_ + 1) % nTabs); return; }
        }
        winMenu_->handleKey(ev);
        if (winMenuClosePending_) closeWinMenu(); // PO powrocie z onSelect — bez UB
        return;
    }

    // --- Globalne skróty klawiszowe — zawsze działają, niezależnie od trybu ---
    // Uwaga: Tab NIE jest globalny — najpierw dostaje go widget (np. menedzer
    // plikow przelacza nim panele), a focusNext() jest fallbackiem gdy widget
    // zwraca Ignored (patrz applyWidgetResult / sekcja Desktop).
    if (ev.key == Key::F1) { openAppMenu(); return; }
    if (ev.ctrl && ev.key == Key::Char && ev.character == U'w') { wm_.closeFocused(); return; }
    if (ev.key == Key::F5 && wm_.focused()) { mode_ = CursorMode::WindowMove; return; }
    if (ev.key == Key::F6 && wm_.focused()) { mode_ = CursorMode::WindowResize; return; }
    if (ev.key == Key::F7 && wm_.focused()) { toggleMaximize(*wm_.focused()); return; }
    if (ev.key == Key::F8 && wm_.focused()) { wm_.focused()->setMinimized(true); return; }

    // --- WindowMove / WindowResize (wyszczególnione najwyzej w trybie, zeby ESC wychodzil natychmiast) ---
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

    // --- Edit mode — klawisze trafiaja do edytowalnego widgetu ---
    if (mode_ == CursorMode::Edit) {
        Window* f = wm_.focused();
        if (!f || f->minimized()) { mode_ = CursorMode::Desktop; return; }

        // F2 = menu aplikacji (dziala tez w trybie edycji).
        if (ev.key == Key::F2) {
            if (f->content() && !f->content()->menuTabs().empty()) openWinMenu(*f, 0);
            return;
        }
        auto* content = f->content();
        // Sprawdz skróty aplikacji PRZED widgetem (np. Ctrl+S dla TextEditor).
        if (content && matchesShortcuts(content->shortcuts(), ev)) {
            invokeShortcut(content->shortcuts(), ev);
            return;
        }
        // Widget dostaje klawisz PIERWSZY: moze zerznac Escape (dialog: anuluj)
        // albo Tab (menedzer: przelaczenie panelu). Fallbacki Esc/Tab obsluguje
        // applyWidgetResult, gdy widget zwraca Ignored.
        applyWidgetResult(f->handleKey(ev), ev);
        return;
    }

    // --- Keyboard mode — nawigacja miedzy widgetami w fokusowanym oknie ---
    if (mode_ == CursorMode::Keyboard) {
        Window* f = wm_.focused();
        if (!f || f->minimized()) { mode_ = CursorMode::Desktop; return; }

        // F2 -> otwiera/przełącza menu konkretnej aplikacji (pierwsza karta).
        if (ev.key == Key::F2) {
            auto& tabs = f->content()->menuTabs();
            if (!tabs.empty()) openWinMenu(*f, 0);
            return;
        }

        // Sprawdz skróty aplikacji PRZED widgetem.
        auto* content = f->content();
        if (content && matchesShortcuts(content->shortcuts(), ev)) {
            invokeShortcut(content->shortcuts(), ev);
            return;
        }

        // Znak -> przechodzimy do trybu Edit dla edytowalnych widgetow.
        if (ev.key == Key::Char && !ev.ctrl && content && content->editable()) {
            mode_ = CursorMode::Edit;
        }

        // Delegujemy do widgetu-zawartosci; Ignored + Esc/Tab = fallbacki globalne
        // (Esc -> pulpit, Tab -> kolejne okno). Widget moze tez zaprosic do Edit
        // zwrotem EnteredEdit (np. LineEdit).
        applyWidgetResult(f->handleKey(ev), ev);
        return;
    }

    // --- Desktop mode — wolny kursor po ekranie ---
    if (ev.key == Key::F2) {
        // Otwiera menu pierwszego okna, na ktorego pasku tytulu stoi kursor;
        // inaczej menu okna fokusa.
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

    // Tab na pulpicie = kolejne okno (widgety w oknach dostaja Tab najpierw —
    // patrz applyWidgetResult — wiec ta sciezka dotyczy trybu Desktop).
    if (ev.key == Key::Tab) { wm_.focusNext(); return; }

    // --- Taskbar — nawigacja i aktywacja okien ------------------
    if (cursor_.y() >= screenHeight() - kTaskBarHeight) {
        const auto& wins = wm_.windows();
        int totalHandles = static_cast<int>(wins.size());
        if (totalHandles == 0) {
            // Pusty taskbar (zadne okno nie otwarte): Down nie schodzi nizej,
            // w gore wraca na pulpit, Left/Right moga sie jeszcze poruszac
            // po pasku, Enter/Space nic nie aktywuje. Wazne: NIE teleportujemy
            // kursora do (0, dol) — tak wlasnie zapadal on w lewy dolny rog
            // i nie dalo sie juz z niego wydostac (ten branch wykonywal sie
            // PRZED obsluga strzalki w gore).
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
            // Porusz sie w lewo o jedno pole; jesli scroll mozliwy i jestes na pocztku — przesun scroll.
            if (canScrollLeft && cursor_.x() <= kTaskHandleW) {
                taskbarScroll_ = std::max(0, taskbarScroll_ - 1);
            } else {
                cursor_.moveBy(-1, 0);
            }
            return;
        }
        if (ev.key == Key::Right) {
            // Porusz sie w prawo o jedno pole; jesli scroll mozliwy i jestes na koncu — przesun scroll.
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
            // Aktywuj okno pod kursorem na taskbarze
            int canScrollL = taskbarScroll_ > 0;
            usableWidth    = screenWidth() - (canScrollL ? 1 : 0) - rightArrowWidth;
            maxVisible     = std::max(1, usableWidth / kTaskHandleW);

            int visualStartX = canScrollL ? 1 : 0; // pozycja pierwszego widocznego uchwytu
            int winIdx       = (cursor_.x() - visualStartX) / kTaskHandleW;
            winIdx += taskbarScroll_;             // dodaj offset scrolla

            if (winIdx >= 0 && winIdx < totalHandles) {
                Window* w = wins[static_cast<std::size_t>(winIdx)].get();
                wm_.focus(w);                       // focus() przywraca zminimalizowane okna
                mode_ = enterWindowMode(*w);         // przejdź do trybu okna
            }
            return;
        }
        // Jeśli kursor jest na taskbarze i użytkownik nacisnął strzałkę w górę,
        // opuść task bar (wróć na pulpit).
        if (ev.key == Key::Up) {
            cursor_.moveTo(cursor_.x(), screenHeight() - 2);
            return;
        }

        // Down — nie rób nic (kursor już jest na taskbarze).
        // Każdy inny klawisz: ignorujemy (głównie Enter/Space, ale to też tu trafia).
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
    for (int y = kMenuBarHeight; y < screen.height() - kTaskBarHeight; ++y)
        for (int x = 0; x < screen.width(); ++x)
            screen.set(x, y, Cell{U' ', t.desktopText, t.desktopBg, 0});
}

void Desktop::drawMenuBar(Screen& screen) {
    const auto& t = theme::active;
    for (int x = 0; x < screen.width(); ++x)
        screen.set(x, 0, Cell{U' ', t.menuBarText, t.menuBarBg, 0});

    std::u32string left = U" Aplikacje (F1) ";
    screen.putText(0, 0, left, t.menuBarText, t.menuBarBg, StyleBold);

    // Tryb w prawym gornym rogu: [tryb] | Fenrir — miejsce gdzie byly "Fenrir".
    std::u32string modeLabel = U"Desktop";
    switch (mode_) {
        case CursorMode::Keyboard:  modeLabel = U"Keyboard"; break;
        case CursorMode::Edit:      modeLabel = U"Edit"; break;
        case CursorMode::WindowMove:   modeLabel = U"Move"; break;
        case CursorMode::WindowResize: modeLabel = U"Resize"; break;
        default: break;
    }
    // Zegar z ustawien: taskbarSeconds = dodatkowe ":SS" na koncu.
    // Nazwa pulpitu tez z ustawien (zmiana w aplikacji Ustawienia od razu
    // widoczna — drawMenuBar czyta settings na bieżaco).
    const std::string& name =
        settings::active.desktopName.empty() ? desktopName_ : settings::active.desktopName;
    std::u32string right = modeLabel + U" | " + text::toUtf32(name);
    int rightX = screen.width() - static_cast<int>(right.size()) - 1;
    if (rightX > static_cast<int>(left.size()))
        screen.putText(rightX, 0, right, t.menuBarText, t.menuBarBg, StyleBold);
}

void Desktop::drawTaskBar(Screen& screen) {
    // Pasek zadan z motywu: taskbarBg tlo, aktywny uchwyt = "wcisniety przycisk"
    // (taskbarActiveBg), akcenty (zegar, strzalki) = highlightText.
    const auto& t = theme::active;
    const Color barBg = t.taskbarBg;
    const Color activeFg = t.taskbarText;
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

    // --- Date/time: ZAWSZE w tym samym miejscu (prawy kraniec, przy strzale >). ---
    // Strefa zegara jest zarezerwowana: uchwytów okien jest tak dużo, żeby nie
    // nachodziły na zegar. Zegar NIGDY się nie przesuwa przy otwieraniu okien.
    time_t now = time(nullptr);
    struct tm *ti = localtime(&now);
    if (ti) {
        // taskbar_seconds=1 → z "sekundami", inaczej jak dotad.
        char tbuf[24];
        const char* fmt = settings::active.taskbarSeconds ? "%Y-%m-%d %H:%M:%S" : "%Y-%m-%d %H:%M";
        strftime(tbuf, sizeof(tbuf), fmt, ti);
        std::string clockStr(tbuf);
        int clockX = screen.width() - 1 - static_cast<int>(clockStr.size()); // przyklejony do prawej
        if (clockX < leftArrowWidth) clockX = leftArrowWidth;
        if (clockX + static_cast<int>(clockStr.size()) <= screen.width()) {
            screen.putText(clockX, y, std::u32string(clockStr.begin(), clockStr.end()), accent, barBg);
        }
    }
}

void Desktop::positionGuiCursor() {
    // W trybie Edit ORAZ Keyboard kursor GUI podazza za wewnetrzna pozycja
    // widgetu (np. kursor malowania / zaznaczona komorka palety w Malowance).
    // W Keyboard wczesniej kursor stawial w oknie — teraz migotanie od razu
    // pokazuje, gdzie sie edytuje/wybiera. Tryb Desktop = wolny wskaznik (bez zmian).
    if (mode_ == CursorMode::Edit || mode_ == CursorMode::Keyboard) {
        Window* f = wm_.focused();
        auto pos = (f && !f->minimized()) ? f->content()->cursorPosition() : std::nullopt;
        if (pos) cursor_.moveTo(pos->first, pos->second);
    }
    // W trybie Desktop/WindowMove/Resize kursor stoi tam, gdzie ostatnio ustawiono.
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
        // Migotanie kursora wylaczalne w ustawieniach (cursor_blink=0 → statyczny).
        cursorBlinkState_ = settings::active.cursorBlink ? !cursorBlinkState_ : true;
    } else {
        cursorBlinkState_ = true;
        for (const KeyEvent& ev : keyParser_.feed(buf, n)) handleKey(ev);
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
        drawBox(screen_, appMenuBounds_, U"Aplikacje");
        appMenu_->draw(screen_);
    }

    // Menu konkretnej aplikacji — dropdown pod paskiem tytułu okna.
    if (winMenu_) {
        drawBox(screen_, winMenuBounds_, winMenuTitle_);
        winMenu_->draw(screen_);
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
