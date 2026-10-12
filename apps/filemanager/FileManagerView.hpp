#pragma once
#include <optional>
#include <string>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/widgets/DirectoryView.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Glowny widok Menedzera plikow: DWA panele (kazdy = DirectoryView z biblioteki
// widgetow) + pasek statusu. Zero wlasnego rysowania list — wszystko przez
// przerazane widgety (DirectoryView, StatusBar). Zapytania do uzytkownika
// (usun/przemianuj/idz do) ida przez AppContext (Dialog/LineEdit z biblioteki).
//
// Model kopiowania jak w klasycznych menedzerach dwupanelowych: Ctrl+C kopiuje
// zaznaczenie z aktywnego panelu DO DRUGIEGO panelu, Ctrl+X je przenosi.
// Brak schowka i wklejania — panel docelowy jest zawsze znany.
class FileManagerView : public Widget {
public:
    FileManagerView(AppContext& ctx, std::string startDir)
        : ctx_(ctx), left_(std::move(startDir)), right_(".") { // prawy panel startuje w biezacym katalogu (cwd)
        auto info = [this](const std::string& path) {
            long long sz = fs::fileSize(path);
            status("Plik: " + path + (sz >= 0 ? " (" + std::to_string(sz) + " B)" : ""));
        };
        left_.onFileActivate = info;
        right_.onFileActivate = info;
        left_.onStatus = [this](const std::string& m) { status(m); };
        right_.onStatus = [this](const std::string& m) { status(m); };
    }

    // --- Operacje wywolywane tez z menu aplikacji ---------------------------

    void goUp() {
        active().goUp();
        status("W gorze: " + active().currentDir());
    }

    void copySelected() {
        DirectoryView& src = active();
        if (src.empty()) { status("Brak elementow."); return; }
        if (!src.selectionIsSafeTarget()) { status("Nie mozna kopiowac '..'."); return; }
        DirectoryView& dest = other();
        const std::string from = src.selectedPath();
        try {
            const std::string to = uniqueNameIn(dest.currentDir(), fs::nameOf(from));
            fs::copyPath(from, to);
            left_.reload();
            right_.reload();
            status("Skopiowano: " + fs::nameOf(from) + " -> " + dest.currentDir());
        } catch (const std::exception& e) {
            status(std::string("Blad: ") + e.what());
        }
    }

    void moveSelected() {
        DirectoryView& src = active();
        if (src.empty()) { status("Brak elementow."); return; }
        if (!src.selectionIsSafeTarget()) { status("Nie mozna przeniesc '..'."); return; }
        DirectoryView& dest = other();
        const std::string from = src.selectedPath();
        try {
            const std::string to = uniqueNameIn(dest.currentDir(), fs::nameOf(from));
            try {
                fs::renamePath(from, to);
            } catch (const std::exception&) {
                // Rozne systemy plikow (np. EXDEV) — rename nie dziala:
                // kopiuj, potem usun zrodlo (sekwencja jak w starych menedzerach).
                fs::copyPath(from, to);
                fs::removePath(from);
            }
            left_.reload();
            right_.reload();
            status("Przeniesiono: " + fs::nameOf(from) + " -> " + dest.currentDir());
        } catch (const std::exception& e) {
            status(std::string("Blad: ") + e.what());
        }
    }

    void renameSelected() {
        DirectoryView& v = active();
        if (v.empty() || !v.selectionIsSafeTarget()) { status("Brak elementow."); return; }
        const std::string oldPath = v.selectedPath();
        const auto token = lifetimeToken();
        ctx_.prompt(U"Przemianuj", U"Nowa nazwa:", U"Nazwa: ",
                    text::toUtf32(fs::nameOf(oldPath)),
                    [this, token, oldPath](std::optional<std::u32string> newName) {
                        if (!token.lock()) return; // okno moglo zostac zamkniete
                        if (!newName || newName->empty()) return;
                        const std::string newBase = text::toUtf8(*newName);
                        const std::string newPath = fs::join(fs::parent(oldPath), newBase);
                        try {
                            if (fs::exists(newPath) && newPath != oldPath) {
                                status("Juz istnieje: " + newBase);
                                return;
                            }
                            fs::renamePath(oldPath, newPath);
                            left_.reload();
                            right_.reload();
                            status("Przemianowano na: " + newBase);
                        } catch (const std::exception& e) {
                            status(std::string("Blad: ") + e.what());
                        }
                    });
    }

    void deleteSelected() {
        DirectoryView& v = active();
        if (v.empty()) { status("Brak elementow."); return; }
        if (!v.selectionIsSafeTarget()) { status("Nie mozna usunac '..'."); return; }
        const std::string path = v.selectedPath();
        const auto token = lifetimeToken();
        ctx_.confirm(U"Usunac na pewno?\n" + text::toUtf32(path),
                     [this, token, path](bool ok) {
                         if (!token.lock()) return;
                         if (!ok) { status("Anulowano."); return; }
                         try {
                             fs::removePath(path);
                             left_.reload();
                             right_.reload();
                             status("Usunieto: " + fs::nameOf(path));
                         } catch (const std::exception& e) {
                             status(std::string("Blad: ") + e.what());
                         }
                     });
    }

    void refreshBoth() {
        left_.reload();
        right_.reload();
        status("Odswiezono oba panele.");
    }

    void goToFolder() {
        const bool toRight = rightActive_; // ktory panel zamawial w chwili otwarcia
        const auto token = lifetimeToken();
        ctx_.openFolder(active().currentDir(), [this, token, toRight](const std::string& picked) {
            if (!token.lock()) return;
            (toRight ? right_ : left_).setDirectory(picked);
            status("Panel: " + picked);
        });
    }

    bool isRightActive() const { return rightActive_; }

    // Minimum: dwa panele maja miec sensowna szerokosc (>=14 na panel)
    // + pasek statusu i kilka wierszy listy.
    int minWidth() const override { return 30; }
    int minHeight() const override { return 6; }

    // --- Widget --------------------------------------------------------------

    void draw(Screen& screen) override {
        const int paneH = std::max(0, bounds_.h - 1);
        const int leftW = bounds_.w / 2;
        left_.setBounds(Rect{bounds_.x, bounds_.y, leftW, paneH});
        right_.setBounds(Rect{bounds_.x + leftW, bounds_.y, bounds_.w - leftW, paneH});
        // Aktywny panel = fokusowany (naglowek ListView podswietla sie na accent).
        left_.setFocused(focused_ && !rightActive_);
        right_.setFocused(focused_ && rightActive_);
        left_.draw(screen);
        right_.draw(screen);

        status_.setBounds(Rect{bounds_.x, bounds_.y + paneH, bounds_.w, 1});
        status_.setText(text::toUtf32(statusMsg_), rightActive_ ? U"Prawy" : U"Lewy");
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Tab/Left/Right = przejscie do drugiego panelu (jak w mc).
        if (ev.key == Key::Tab ||
            (ev.key == Key::Left && !ev.ctrl) ||
            (ev.key == Key::Right && !ev.ctrl)) {
            rightActive_ = !rightActive_;
            return KeyEventResult::Consumed;
        }
        if (ev.ctrl && ev.key == Key::Char) {
            switch (ev.character) {
                case U'c': case U'C': copySelected();  return KeyEventResult::Consumed;
                case U'x': case U'X': moveSelected();   return KeyEventResult::Consumed;
                case U'r': case U'R': renameSelected(); return KeyEventResult::Consumed;
                case U'g': case U'G': goToFolder();     return KeyEventResult::Consumed;
                default: break;
            }
        }
        if (ev.key == Key::Delete) { deleteSelected(); return KeyEventResult::Consumed; }
        return active().handleKey(ev); // nawigacja/Enter/Backspace idzie do panelu
    }

private:
    DirectoryView& active() { return rightActive_ ? right_ : left_; }
    DirectoryView& other() { return rightActive_ ? left_ : right_; }

    void status(const std::string& msg) { statusMsg_ = msg; }

    // "raport.txt" gdy zajety -> "raport (1).txt" itd. (jak w starym menedzerze).
    static std::string uniqueNameIn(const std::string& dir, const std::string& base) {
        std::string dst = fs::join(dir, base);
        if (!fs::exists(dst)) return dst;
        auto dot = base.find_last_of('.');
        const bool hasExt = dot != std::string::npos && dot != base.size() - 1 && dot != 0;
        const std::string stem = hasExt ? base.substr(0, dot) : base;
        const std::string ext = hasExt ? base.substr(dot) : "";
        for (int n = 1;; ++n) {
            dst = fs::join(dir, stem + " (" + std::to_string(n) + ")" + ext);
            if (!fs::exists(dst)) return dst;
        }
    }

    AppContext& ctx_;
    DirectoryView left_;
    DirectoryView right_;
    bool rightActive_ = false;

    StatusBar status_;
    std::string statusMsg_ =
        "Tab: panel | Ctrl+C: kopiuj do drugiego | Ctrl+X: przenies | Del: usun | Ctrl+R: nazwa | Ctrl+G: idz do";
};

} // namespace fenrir::apps
