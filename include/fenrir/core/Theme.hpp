#pragma once
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

    // --- naglowki list ---
    Color headerBg{colors::Gray};
    Color headerText{colors::White};

    // --- przyciski ---
    Color buttonBg{colors::Gray};
    Color buttonText{colors::Black};
    Color buttonTextHot{colors::White}; // fokus / zaznaczenie

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

} // namespace fenrir::theme
