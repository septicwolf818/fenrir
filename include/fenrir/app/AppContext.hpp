#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "fenrir/widgets/Dialog.hpp"

namespace fenrir {

// To jest "API dialogow" - jedyny sposob, w jaki aplikacja pokazuje
// uzytkownikowi dialog wyboru pliku/folderu albo komunikat/potwierdzenie/prompt.
// Implementuje je Desktop (patrz desktop/Desktop.hpp); aplikacje dostaja
// referencje do AppContext w App::createView(AppContext&) i NIE wiedza nic
// wiecej o tym, jak dialog jest zaimplementowany (framework tworzy okno,
// rozmiaruje je i zamyka po wyniku — aplikacja dostaje tylko callback).
//
// Wszystkie metody sa asynchroniczne: otwieraja okno i wracaja natychmiast;
// wynik przychodzi pozniej przez callback (lub wcale, jesli uzytkownik anuluje).
class AppContext {
public:
    virtual ~AppContext() = default;

    using OnPick = std::function<void(const std::string& path)>;
    using OnCancel = std::function<void()>;
    // Wynik dialogu: idx = numer przycisku, -1 = Esc/anuluj.
    using OnDialogResult = std::function<void(int index)>;

    // Dialog wyboru PLIKU do otwarcia (np. Ctrl+O w edytorze).
    virtual void openFile(const std::string& startDir, OnPick onPick, OnCancel onCancel = {}) = 0;

    // Dialog wyboru FOLDERU (np. "Idz do folderu..." w Menedzerze plikow).
    virtual void openFolder(const std::string& startDir, OnPick onPick, OnCancel onCancel = {}) = 0;

    // Dialog zapisu: przegladanie + pole nazwy pliku (np. "Zapisz jako...").
    virtual void saveFile(const std::string& startDir, const std::string& suggestedName,
                           OnPick onPick, OnCancel onCancel = {}) = 0;

    // Otwiera okno dialogowe z widgetem Dialog (komunikat / potwierdzenie /
    // prompt). Framework rozmiaruje okno, zamyka je po wyniku i zwraca fokus
    // do okna wystawiajacego.
    virtual void openDialog(std::u32string title, std::unique_ptr<Dialog> content,
                            OnDialogResult onResult) = 0;

    // --- Asynchroniczne zrodla danych (np. PTY terminala) --------------------
    // Rejestruje fd sprawdzany co tick (poll POLLIN, bez blokowania); onReady
    // jest wolane gdy na fd sa dane. Zrodlo MUSA sie usunac (removeFdSource)
    // ZWROT zanim fd zostanie zamkniete — destruktor widgetu jest miejscem.
    virtual void addFdSource(int fd, std::function<void()> onReady) = 0;
    virtual void removeFdSource(int fd) = 0;

    // --- Wysokopoziomowe helpery (buduja Dialog i otwieraja go przez openDialog) ---

    // Komunikat "OK" (np. "O kalkulatorze").
    void showMessage(std::u32string text, std::function<void()> onOk = {}) {
        auto dlg = std::make_unique<Dialog>(std::move(text), Dialog::Buttons::Ok);
        openDialog(U"Informacja", std::move(dlg),
                   [onOk = std::move(onOk)](int idx) {
                       if (idx == 0 && onOk) onOk();
                   });
    }

    // Pytanie tak/nie (OK/Anuluj): true = OK, false = Anuluj/Esc.
    void confirm(std::u32string text, std::function<void(bool)> onAnswer) {
        auto dlg = std::make_unique<Dialog>(std::move(text), Dialog::Buttons::OkCancel);
        openDialog(U"Potwierdzenie", std::move(dlg),
                   [onAnswer = std::move(onAnswer)](int idx) {
                       if (onAnswer) onAnswer(idx == 0);
                   });
    }

    // Prompt z polem tekstowym: nullopt = anuluj/Esc, wartosc = wpisany tekst.
    // Wskaznik na LineEdit zyje az do zamkniecia okna — callback jest wolany
    // wlasnie wtedy (przed zamknieciem), wiec odczyt wartosci jest bezpieczny.
    void prompt(std::u32string title, std::u32string message, std::u32string label,
                std::u32string initial,
                std::function<void(std::optional<std::u32string>)> onText) {
        auto dlg = std::make_unique<Dialog>(std::move(message), Dialog::Buttons::OkCancel);
        LineEdit* field = dlg->setInput(std::move(label), std::move(initial));
        openDialog(std::move(title), std::move(dlg),
                   [field, onText = std::move(onText)](int idx) {
                       if (!onText) return;
                       if (idx == 0) onText(field->value());
                       else onText(std::nullopt);
                   });
    }
};

} // namespace fenrir
