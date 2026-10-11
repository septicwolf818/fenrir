#include "fenrir/core/Theme.hpp"

namespace fenrir::theme {

// Katalog motywow. Kazdy motyw to pelna paleta ról (Theme) + stabilne id
// (klucz w ustawieniach) + nazwa wyswietlana. Kolejnosc = kolejnosc cykla
// w aplikacji Ustawienia (Enter na wierszu "Motyw").
//
// Wszystkie kolory SA z palety ANSI 16 (Linux TTY, zero truecolor) —
// renderer wysyla indeks bezposrednio jako SGR.

const std::vector<NamedTheme>& catalog() {
    static const std::vector<NamedTheme> cats = {
        // Klasyczny — domyslny, szary pulpit i niebieskie paski (jak stare
        // okienkowe systemy, na ktorych wzorowal sie projekt od poczatku).
        {"classic", "Klasyczny", Theme{}},

        // Północ — ciemny, granatowo-cyjanowy "nocny" motyw.
        {"midnight", "Północ", Theme{
            .desktopBg = colors::Black, .desktopText = colors::BrightCyan,
            .menuBarBg = colors::Blue, .menuBarText = colors::BrightWhite,
            .taskbarBg = colors::Black, .taskbarText = colors::Cyan,
            .taskbarActiveBg = colors::Blue, .highlightText = colors::BrightYellow,
            .border = colors::Cyan, .borderInactive = colors::Blue,
            .titleBg = colors::Blue, .titleText = colors::BrightYellow,
            .closeBg = colors::Red, .closeText = colors::BrightWhite,
            .windowBg = colors::Black, .windowText = colors::Cyan,
            .menuBg = colors::Blue, .menuText = colors::BrightWhite,
            .accentBg = colors::Blue, .accentText = colors::BrightYellow,
            .surfaceBg = colors::Black, .surfaceText = colors::BrightCyan,
            .headerBg = colors::Blue, .headerText = colors::BrightYellow,
            .buttonBg = colors::Blue, .buttonText = colors::Cyan,
            .buttonTextHot = colors::BrightYellow,
            .fieldBg = colors::Black, .fieldText = colors::BrightYellow,
            .statusBg = colors::Blue, .statusText = colors::BrightYellow,
            .editorBg = colors::Black, .editorText = colors::BrightCyan,
            .errorText = colors::BrightRed,
        }},

        // Bursztyn — monochromatyczny motyw "starego monitora bursztynowego".
        {"amber", "Bursztyn", Theme{
            .desktopBg = colors::Black, .desktopText = colors::Yellow,
            .menuBarBg = colors::Black, .menuBarText = colors::BrightYellow,
            .taskbarBg = colors::Black, .taskbarText = colors::Yellow,
            .taskbarActiveBg = colors::Yellow, .highlightText = colors::BrightYellow,
            .border = colors::Yellow, .borderInactive = colors::Gray,
            .titleBg = colors::Yellow, .titleText = colors::Black,
            .closeBg = colors::BrightRed, .closeText = colors::Black,
            .windowBg = colors::Black, .windowText = colors::Yellow,
            .menuBg = colors::Black, .menuText = colors::BrightYellow,
            .accentBg = colors::Yellow, .accentText = colors::Black,
            .surfaceBg = colors::Black, .surfaceText = colors::BrightYellow,
            .headerBg = colors::Yellow, .headerText = colors::Black,
            .buttonBg = colors::Black, .buttonText = colors::Yellow,
            .buttonTextHot = colors::BrightYellow,
            .fieldBg = colors::Black, .fieldText = colors::BrightYellow,
            .statusBg = colors::Black, .statusText = colors::Yellow,
            .editorBg = colors::Black, .editorText = colors::BrightYellow,
            .errorText = colors::BrightRed,
        }},

        // Leśny — zielony terminal, czarne tlo + zielone ramki i naglowki.
        {"forest", "Leśny", Theme{
            .desktopBg = colors::Black, .desktopText = colors::Green,
            .menuBarBg = colors::Green, .menuBarText = colors::Black,
            .taskbarBg = colors::Black, .taskbarText = colors::Green,
            .taskbarActiveBg = colors::Green, .highlightText = colors::BrightGreen,
            .border = colors::Green, .borderInactive = colors::Gray,
            .titleBg = colors::Green, .titleText = colors::Black,
            .closeBg = colors::BrightRed, .closeText = colors::White,
            .windowBg = colors::Black, .windowText = colors::Green,
            .menuBg = colors::Green, .menuText = colors::Black,
            .accentBg = colors::Green, .accentText = colors::Black,
            .surfaceBg = colors::Black, .surfaceText = colors::BrightGreen,
            .headerBg = colors::Green, .headerText = colors::Black,
            .buttonBg = colors::Black, .buttonText = colors::Green,
            .buttonTextHot = colors::BrightGreen,
            .fieldBg = colors::Black, .fieldText = colors::BrightGreen,
            .statusBg = colors::Green, .statusText = colors::Black,
            .editorBg = colors::Black, .editorText = colors::BrightGreen,
            .errorText = colors::BrightRed,
        }},

        // Wysoki kontrast — czern/biel + zolte akcenty (czytelnosc przede wszystkim).
        {"contrast", "Wysoki kontrast", Theme{
            .desktopBg = colors::Black, .desktopText = colors::BrightWhite,
            .menuBarBg = colors::Black, .menuBarText = colors::BrightYellow,
            .taskbarBg = colors::Black, .taskbarText = colors::BrightWhite,
            .taskbarActiveBg = colors::Gray, .highlightText = colors::BrightYellow,
            .border = colors::BrightWhite, .borderInactive = colors::Gray,
            .titleBg = colors::White, .titleText = colors::Black,
            .closeBg = colors::BrightRed, .closeText = colors::BrightWhite,
            .windowBg = colors::Black, .windowText = colors::BrightWhite,
            .menuBg = colors::Black, .menuText = colors::BrightYellow,
            .accentBg = colors::White, .accentText = colors::Black,
            .surfaceBg = colors::Black, .surfaceText = colors::BrightWhite,
            .headerBg = colors::White, .headerText = colors::Black,
            .buttonBg = colors::Black, .buttonText = colors::BrightWhite,
            .buttonTextHot = colors::BrightYellow,
            .fieldBg = colors::Black, .fieldText = colors::BrightWhite,
            .statusBg = colors::Black, .statusText = colors::BrightYellow,
            .editorBg = colors::Black, .editorText = colors::BrightWhite,
            .errorText = colors::BrightRed,
        }},
    };
    return cats;
}

const NamedTheme* find(std::string_view id) {
    for (const auto& n : catalog()) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

bool apply(std::string_view id) {
    const NamedTheme* n = find(id);
    if (!n) return false;
    active = n->theme;
    return true;
}

} // namespace fenrir::theme
