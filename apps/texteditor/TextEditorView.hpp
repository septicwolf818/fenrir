#pragma once
#include <memory>
#include <string>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/TextArea.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Widok edytora = TextArea + StatusBar z biblioteki widgetow. Pasek pokazuje
// komunikat (wczytanie/zapis/blad — az do pierwszego klawisza), potem nazwe
// pliku, pozycje kursora (Wiersz:Kolumna) i gwiazdke gdy plik jest
// zmodyfikowany. Nowy/Otworz przy brudnym pliku pytaja o potwierdzenie
// (AppContext::confirm).
class TextEditorView : public Widget {
public:
    TextEditorView(AppContext& ctx, std::string startPath) : ctx_(ctx) {
        area_.onStatus = [this](const std::string& m) { statusMsg_ = m; };
        area_.loadFile(std::move(startPath));
    }

    // --- Operacje wywolywane z menu / skrotow --------------------------------

    void newFile() { guardDirty([this] { area_.newFile(); }); }

    void openFile() {
        guardDirty([this] {
            std::string startDir = area_.path().empty() ? fs::homeDir() : fs::parent(area_.path());
            const auto token = lifetimeToken();
            ctx_.openFile(startDir, [this, token](const std::string& picked) {
                if (!token.lock()) return;
                area_.loadFile(picked);
            });
        });
    }

    void save() {
        if (area_.path().empty()) { saveAs(); return; }
        area_.saveToCurrentPath();
    }

    void saveAs() {
        std::string startDir = area_.path().empty() ? fs::homeDir() : fs::parent(area_.path());
        // Podsuwamy nazwe BIEZACEGO pliku (dokumentN.txt), a nie staly
        // "dokument.txt" — inaczej "Zapisz jako" podsunalaby zla nazwe.
        std::string suggest =
            area_.path().empty() ? std::string("dokument.txt") : fs::nameOf(area_.path());
        const auto token = lifetimeToken();
        ctx_.saveFile(startDir, suggest, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            area_.saveFile(picked);
        });
    }

    // Przelacza owijanie linii (Ctrl+E) — wylaczone = przewijanie poziome dlugich linii.
    void toggleWrap() { area_.toggleWrap(); }

    // --- Widget --------------------------------------------------------------

    void draw(Screen& screen) override {
        const int statusH = 1;
        const int areaH = std::max(0, bounds_.h - statusH);
        area_.setBounds(Rect{bounds_.x, bounds_.y, bounds_.w, areaH});
        area_.setFocused(focused_);
        area_.draw(screen);

        auto [row, col] = area_.cursorLineCol();
        std::u32string left = statusMsg_.empty()
            ? text::toUtf32(std::string("Plik: ") +
                            (area_.path().empty() ? std::string("(nowy)") : area_.path()))
            : text::toUtf32(statusMsg_);
        std::u32string right = text::toUtf32(std::to_string(row) + ":" + std::to_string(col));
        if (area_.dirty()) right += U" *";

        status_.setBounds(Rect{bounds_.x, bounds_.y + areaH, bounds_.w, statusH});
        status_.setText(std::move(left), std::move(right));
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Komunikat statusu (wczytanie/zapis/blad/owijanie) jest przelotowy:
        // znika przy pierwszym klawiszu, zeby pasek wrocil do "Plik: <sciezka>"
        // (Ctrl+E ustawia nowy komunikat PO tym czyszczeniu — patrz toggleWrap).
        statusMsg_.clear();
        return area_.handleKey(ev);
    }

    bool editable() const override { return area_.editable(); }

    // Niezapisane zmiany pliku — Desktop pyta o potwierdzenie, zanim zamknie
    // okno z brudna trescia (guard zamykania). Delegujemy do TextArea.
    bool dirty() const override { return area_.dirty(); }

    std::optional<std::pair<int, int>> cursorPosition() const override { return area_.cursorPosition(); }

    // Minimum tresci: pare wierszy tekstu + pasek statusu, czytelna kolumna.
    int minWidth() const override { return 20; }
    int minHeight() const override { return 5; }

private:
    // Jesli plik zmodyfikowany — pytamy, czy kontynuowac; potem `proceed`.
    void guardDirty(std::function<void()> proceed) {
        if (!area_.dirty()) { proceed(); return; }
        const auto token = lifetimeToken();
        ctx_.confirm(U"Niezapisane zmiany. Kontynuowac?",
                     [token, proceed = std::move(proceed)](bool ok) {
                         if (!token.lock()) return; // okno moglo zostac zamkniete
                         if (ok) proceed();
                     });
    }

    AppContext& ctx_;
    TextArea area_;
    StatusBar status_;
    std::string statusMsg_;
};

} // namespace fenrir::apps
