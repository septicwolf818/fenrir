#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "fenrir/core/Types.hpp"

namespace fenrir::theme {

// Semantyczne role kolorow. Kod (widgety, okna, aplikacje) odwoluje sie do ROLI,
// nigdy bezposrednio do konkretnej palety — dzieki temu caly motyw wymienia sie
// przez podmiane jednej zmiennej `active`.
// Wszystkie role sa kolorami z palety ANSI 16 (Linux TTY, zero truecolor).
struct Theme {
    // --- pulpit ---
    Color desktopBg{colors::Black};
    Color desktopText{colors::White};

    // --- pasek menu u gory ekranu ---
    Color menuBarBg{colors::Blue};
    Color menuBarText{colors::White};

    // --- pasek zadan u dolu ---
    Color taskbarBg{colors::Black};
    Color taskbarText{colors::White};
    Color taskbarActiveBg{colors::Gray};
    Color highlightText{colors::Yellow}; // zegar, strzalki, akcenty tekstu

    // --- okno ---
    Color border{colors::White};           // ramka aktywnego okna
    Color borderInactive{colors::Gray};
    Color titleBg{colors::Blue};
    Color titleText{colors::White};
    Color closeBg{colors::Red};
    Color closeText{colors::White};
    Color windowBg{colors::Gray};          // tlo zawartosci okna
    Color windowText{colors::White};

    // --- menu w oknie (karty) i ramki dropdownow ---
    Color menuBg{colors::Gray};
    Color menuText{colors::White};

    // --- zaznaczenie / element akcentowy ---
    Color accentBg{colors::Blue};
    Color accentText{colors::White};

    // --- powierzchnie: panele list, wnetrza dialogow, wiersze menu ---
    Color surfaceBg{colors::White};
    Color surfaceText{colors::Black};
    Color dimText{colors::Gray}; // przygaszone wiersze (np. "..") na surfaceBg

    // --- naglowki list ---
    Color headerBg{colors::Gray};
    Color headerText{colors::White};

    // --- przyciski ---
    // Uwaga: Gray{8} to "bright black" (ciemny szary) — na tle okna wychodzi
    // czarne-na-czarnym. Klasyczny motyw uzywa White{7} (jasny szary) + czarny
    // tekst; hot = niebieski tekst na jasnym tle.
    Color buttonBg{colors::White};
    Color buttonText{colors::Black};
    Color buttonTextHot{colors::Blue}; // fokus / zaznaczenie

    // --- pola tekstowe ---
    Color fieldBg{colors::White};
    Color fieldText{colors::Black};

    // --- pasek statusu / podpowiedzi ---
    Color statusBg{colors::Gray};
    Color statusText{colors::White};

    // --- powierzchnia edycji tekstu ---
    Color editorBg{colors::Black};
    Color editorText{colors::White};

    // --- bledy ---
    Color errorText{colors::BrightRed};
};

// Aktualny motyw. Podmiana = nowy motyw dla calego UI.
inline Theme active{};

// Motyw z katalogu — id (stabilny klucz w ustawieniach, np. "classic") +
// nazwa wyswietlana (np. "Klasyczny") + pelna paleta ról.
struct NamedTheme {
    std::string id;
    std::string label;
    Theme theme;
};

// Katalog motywow dostepnych w systemie (Ustawienia cykla po nim Enterem).
const std::vector<NamedTheme>& catalog();

// Motyw o danym id albo nullptr (nieznane id).
const NamedTheme* find(std::string_view id);

// Podmienia `active` na motyw o danym id. Nieznane id = bez zmian.
// Zwraca true, jesli motyw zostal zastosowany.
bool apply(std::string_view id);

} // namespace fenrir::theme
