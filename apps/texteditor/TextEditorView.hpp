#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Prefs.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/widgets/FileTree.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/TabBar.hpp"
#include "fenrir/widgets/TextArea.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Widok edytora: pasek kart (TabBar) + TextArea + StatusBar w jednym oknie.
// Kazda otwarta karta = wlasna TextArea (osobny bufor, kursor, sciezka).
// Ctrl+N = nowa karta (nic nie tracimy — brak guardu, bo to nowy bufor),
// Ctrl+O = otworz w nowej karcie (tez juz otwarta = przelacza), Ctrl+T =
// zamknij karte (brudna pyta o potwierdzenie), F4/F9 = nastepna/poprzednia.
// dirty() = CZYKOLWIEK karta brudna — guard zamykania okna chroni wszystko.
// Ustawienia (numery linii, spacje zamiast taba) wspolne dla kart, trwale
// w prefs::forApp("editor").
class TextEditorView : public Widget {
public:
    TextEditorView(AppContext& ctx, std::string startPath) : ctx_(ctx) {
        auto& p = prefs::forApp("editor");
        lineNumbers_ = p.getBool("line_numbers", false);
        insertsSpaces_ = p.getBool("insert_spaces", false);
        openNewTab(std::move(startPath));
        refreshTabBar();
        // Enter w drzewie otwiera plik w nowej karcie (juz otwarta = przelacz)
        // i przenosi fokus do tekstu; Ctrl+B wraca do drzewa.
        tree_.onOpen = [this](const std::string& path) {
            openInEditor(path);
            treeFocused_ = false;
            statusMsg_ = "Otwarto: " + fs::nameOf(path);
        };
    }

    // --- Karty ----------------------------------------------------------------

    void newFile() { openNewTab(""); }

    void openFile() {
        std::string startDir = cur().path().empty() ? fs::homeDir() : fs::parent(cur().path());
        const auto token = lifetimeToken();
        ctx_.openFile(startDir, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            openInEditor(picked);
        });
    }

    void closeTab() {
        auto close = [this] {
            tabs_.erase(tabs_.begin() + current_);
            if (tabs_.empty()) openNewTab("");
            if (current_ >= static_cast<int>(tabs_.size()))
                current_ = static_cast<int>(tabs_.size()) - 1;
            refreshTabBar();
            statusMsg_ = "Zamknieto karte";
        };
        if (!cur().dirty()) { close(); return; }
        const auto token = lifetimeToken();
        ctx_.confirm(U"Niezapisane zmiany. Zamknac karte?",
                     [token, close = std::move(close)](bool ok) {
                         if (!token.lock()) return;
                         if (ok) close();
                     });
    }

    void nextTab() { switchTo(current_ + 1); }
    void prevTab() { switchTo(current_ - 1); }

    // --- Folder / drzewo plikow ------------------------------------------------

    // Ctrl+D: otwiera dialog wyboru folderu; po wyborze panel drzewa po
    // lewej stronie (FileTree), zaznaczony korzen, fokus w drzewie.
    void openFolder() {
        std::string startDir = cur().path().empty() ? fs::homeDir() : fs::parent(cur().path());
        const auto token = lifetimeToken();
        ctx_.openFolder(startDir, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            tree_.setRoot(picked);
            treeOpen_ = true;
            treeFocused_ = true;
            statusMsg_ = "Folder: " + picked;
        });
    }

    // Ctrl+B: przelacza fokus drzewo <-> tekst; gdy drzewo nieotwarte —
    // zachowuje sie jak Ctrl+D (od razu dialog).
    void toggleTreeFocus() {
        if (!treeOpen_) {
            openFolder();
            return;
        }
        treeFocused_ = !treeFocused_;
        statusMsg_ = treeFocused_ ? "Panel plikow" : "Edycja tekstu";
    }

    // --- Operacje plikowe (bierzaca karta) ------------------------------------

    void save() {
        if (cur().path().empty()) { saveAs(); return; }
        cur().saveToCurrentPath();
        refreshTabBar(); // po zapisie nowego pliku zmienia sie etykieta
    }

    void saveAs() {
        // Podsuwamy nazwe BIEZACEGO pliku (dokumentN.txt), a nie staly
        // "dokument.txt" — inaczej "Zapisz jako" podsunalaby zla nazwe.
        std::string startDir = cur().path().empty() ? fs::homeDir() : fs::parent(cur().path());
        std::string suggest =
            cur().path().empty() ? std::string("dokument.txt") : fs::nameOf(cur().path());
        const auto token = lifetimeToken();
        ctx_.saveFile(startDir, suggest, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            cur().saveFile(picked);
            refreshTabBar();
        });
    }

    // --- Format / preferencje --------------------------------------------------

    void toggleWrap() { cur().toggleWrap(); }

    // Numery linii (Ctrl+G) — gutter; zapisywane w preferencjach edytora.
    void toggleLineNumbers() {
        lineNumbers_ = !lineNumbers_;
        applyToAll([](TextArea& a, bool ln, bool) { a.setShowLineNumbers(ln); });
        prefs::forApp("editor").setBool("line_numbers", lineNumbers_);
        statusMsg_ = lineNumbers_ ? "Numery linii: wlaczone" : "Numery linii: wylaczone";
    }

    // Tab wstawia spacje zamiast tabulatora (Ctrl+Y); zapisywane w preferencjach.
    void toggleSpaces() {
        insertsSpaces_ = !insertsSpaces_;
        applyToAll([](TextArea& a, bool, bool sp) { a.setInsertsSpaces(sp); });
        prefs::forApp("editor").setBool("insert_spaces", insertsSpaces_);
        statusMsg_ = insertsSpaces_ ? "Tab = spacje (4): wlaczone" : "Tab = tabulator: spacje wylaczone";
    }

    // --- Widget ----------------------------------------------------------------

    void draw(Screen& screen) override {
        const int statusH = 1, tabH = 1;
        const int bodyH = std::max(0, bounds_.h - statusH);
        const int areaH = std::max(0, bodyH - tabH);

        // Drzewo po lewej (gdy otwarte), prawa kolumna: karty + tekst.
        const int treeW = treeWidth();
        if (treeOpen_ && treeW > 0) {
            tree_.setBounds(Rect{bounds_.x, bounds_.y, treeW, bodyH});
            tree_.setFocused(focused_ && treeFocused_);
            tree_.draw(screen);
        }

        const int rx = bounds_.x + treeW;
        const int rw = bounds_.w - treeW;
        tabBar_.setBounds(Rect{rx, bounds_.y, rw, tabH});
        tabBar_.draw(screen);

        cur().setBounds(Rect{rx, bounds_.y + tabH, rw, areaH});
        cur().setFocused(focused_ && !treeFocused_);
        cur().draw(screen);

        auto [row, col] = cur().cursorLineCol();
        std::u32string left = statusMsg_.empty()
            ? text::toUtf32(std::string("Plik: ") +
                            (cur().path().empty() ? std::string("(nowy)") : cur().path())) +
                  U" [" + text::toUtf32(std::to_string(current_ + 1)) + U"/" +
                  text::toUtf32(std::to_string(tabs_.size())) + U"]"
            : text::toUtf32(statusMsg_);
        std::u32string right = text::toUtf32(std::to_string(row) + ":" + std::to_string(col));
        if (cur().dirty()) right += U" *";

        status_.setBounds(Rect{bounds_.x, bounds_.y + bodyH, bounds_.w, statusH});
        status_.setText(std::move(left), std::move(right));
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Komunikat statusu (wczytanie/zapis/blad/owijanie) jest przelotowy:
        // znika przy pierwszym klawiszu, zeby pasek wrocil do "Plik: <sciezka>".
        statusMsg_.clear();
        if (treeOpen_ && treeFocused_) {
            // Tab z drzewa wraca do tekstu (Alt+Tab tez — ale przechwytuje
            // go Desktop, nie drzewo). Esc celowo NIE — rezerwuje go Desktop
            // na zmiane trybow kursora (Edit -> Keyboard -> Desktop).
            if (ev.key == Key::Tab && !ev.ctrl && !ev.alt) {
                treeFocused_ = false;
                return KeyEventResult::Consumed;
            }
            return tree_.handleKey(ev);
        }
        return cur().handleKey(ev);
    }

    bool editable() const override { return cur().editable(); }

    // Niezapisane zmiany — Desktop pyta przed zamknieciem okna. Brudna jest
    // dowolna karta (zamykanie okna traci wszystkie — strzegimy calosci).
    bool dirty() const override {
        for (const auto& t : tabs_)
            if (t->dirty()) return true;
        return false;
    }

    // Klik wirtualnym kursorem (Enter/Spacja w trybie Desktop):
    // - panel drzewa: fokus do drzewa i wejscie do okna (false = Desktop
    //   wlacza tryb okna; klawisze trafia do drzewa, bo treeFocused_),
    // - pas kart: przelecza na karte pod kursorem (true),
    // - reszta tresci (tekst): fokus w tekscie, standardowe wejscie (false).
    bool clickAt(int x, int y) override {
        const int treeW = treeWidth();
        const int bodyH = std::max(0, bounds_.h - 1); // bez paska statusu
        if (treeOpen_ && treeW > 0 &&
            x >= bounds_.x && x < bounds_.x + treeW &&
            y >= bounds_.y && y < bounds_.y + bodyH) {
            treeFocused_ = true;
            return false; // wejscie do okna z fokusem w drzewie
        }
        treeFocused_ = false; // klik w tresc = fokus w tekscie
        if (tabBar_.count() == 0 || y != tabBar_.bounds().y) return false;
        if (tabBar_.scrollHit(x)) return true; // przyciski <> — scroll bez zmiany karty
        const int i = tabBar_.tabAt(x);
        if (i < 0) return false;
        if (i != current_) switchTo(i);
        return true;
    }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        if (treeOpen_ && treeFocused_) return std::nullopt; // kursor GUI w drzewie? nie
        return cur().cursorPosition();
    }

    // Minimum: karta + pare wierszy tekstu + pasek statusu.
    int minWidth() const override { return 20; }
    int minHeight() const override { return 6; }

private:
    TextArea& cur() { return *tabs_[static_cast<std::size_t>(current_)]; }
    const TextArea& cur() const { return *tabs_[static_cast<std::size_t>(current_)]; }

    // Szerokosc panelu drzewa — ta sama geometria w draw() i clickAt().
    int treeWidth() const {
        if (!treeOpen_) return 0;
        int treeW = std::clamp(bounds_.w / 3, 12, 28);
        if (bounds_.w - treeW < 20) treeW = std::max(0, bounds_.w - 20);
        return treeW;
    }

    void openNewTab(const std::string& path) {
        auto area = std::make_unique<TextArea>();
        area->onStatus = [this](const std::string& m) { statusMsg_ = m; };
        area->setShowLineNumbers(lineNumbers_);
        area->setInsertsSpaces(insertsSpaces_);
        if (!path.empty()) area->loadFile(path);
        tabs_.push_back(std::move(area));
        current_ = static_cast<int>(tabs_.size()) - 1;
        refreshTabBar();
    }

    void openInEditor(const std::string& path) {
        // Juz otwarta? Przelaczamy zamiast dublowac karte.
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (tabs_[static_cast<std::size_t>(i)]->path() == path) {
                switchTo(i);
                return;
            }
        }
        openNewTab(path);
    }

    void switchTo(int idx) {
        if (tabs_.empty()) return;
        const int n = static_cast<int>(tabs_.size());
        current_ = ((idx % n) + n) % n;
        refreshTabBar(); // odswieza tez zaznaczenie paska i markery dirty
    }

    void refreshTabBar() {
        std::vector<TabBar::Tab> tabs;
        tabs.reserve(tabs_.size());
        int unnamed = 0;
        for (const auto& area : tabs_) {
            const std::string& p = area->path();
            std::u32string label = p.empty()
                ? U"(nowy " + text::toUtf32(std::to_string(++unnamed)) + U")"
                : text::toUtf32(fs::nameOf(p));
            tabs.push_back(TabBar::Tab{std::move(label), area->dirty()});
        }
        tabBar_.setTabs(std::move(tabs));
        tabBar_.select(current_);
    }

    template <typename F>
    void applyToAll(F&& f) {
        for (auto& t : tabs_) f(*t, lineNumbers_, insertsSpaces_);
    }

    AppContext& ctx_;
    std::vector<std::unique_ptr<TextArea>> tabs_;
    int current_ = 0;
    TabBar tabBar_;
    FileTree tree_;
    bool treeOpen_ = false;
    bool treeFocused_ = false;
    StatusBar status_;
    std::string statusMsg_;
    bool lineNumbers_ = false;
    bool insertsSpaces_ = false;
};

} // namespace fenrir::apps
