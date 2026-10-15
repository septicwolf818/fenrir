#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "fenrir/core/Types.hpp"

namespace fenrir::theme {

// Semantic color roles. Code (widgets, windows, applications) refers to ROLES,
// never directly to a concrete palette - so the whole theme is swapped
// by replacing a single variable, `active`.
// All roles are colors from the 16-color ANSI palette (Linux TTY, no truecolor).
struct Theme {
    // --- desktop ---
    Color desktopBg{colors::Black};
    Color desktopText{colors::White};

    // --- menu bar at the top of the screen ---
    Color menuBarBg{colors::Blue};
    Color menuBarText{colors::White};

    // --- taskbar at the bottom ---
    Color taskbarBg{colors::Black};
    Color taskbarText{colors::White}; // inactive window handles
    Color taskbarActiveBg{colors::Gray};
    Color taskbarActiveText{colors::White}; // separate pair keeps active handles readable
    Color highlightText{colors::Yellow}; // clock, arrows, text accents

    // --- window ---
    Color border{colors::White};           // active window frame
    Color borderInactive{colors::Gray};
    Color titleBg{colors::Blue};
    Color titleText{colors::White};
    Color closeBg{colors::Red};
    Color closeText{colors::White};
    Color windowBg{colors::Gray};          // window content background
    Color windowText{colors::White};

    // --- in-window menu (tabs) and dropdown frames ---
    Color menuBg{colors::Gray};
    Color menuText{colors::White};

    // --- selection / accent element ---
    Color accentBg{colors::Blue};
    Color accentText{colors::White};

    // --- surfaces: list panels, dialog interiors, menu rows ---
    Color surfaceBg{colors::White};
    Color surfaceText{colors::Black};
    Color dimText{colors::Gray}; // dimmed rows (e.g. "..") on surfaceBg

    // --- list headers ---
    Color headerBg{colors::Gray};
    Color headerText{colors::White};

    // --- buttons ---
    // Note: Gray{8} is "bright black" (dark gray) - against the window background
    // it renders black-on-black. The classic theme uses White{7} (light gray) + black
    // text; hot = blue text on a light background.
    Color buttonBg{colors::White};
    Color buttonText{colors::Black};
    Color buttonTextHot{colors::Blue}; // focus / selection

    // --- text fields ---
    Color fieldBg{colors::White};
    Color fieldText{colors::Blue};

    // --- status bar / hints ---
    Color statusBg{colors::Gray};
    Color statusText{colors::White};

    // --- text editing surface ---
    Color editorBg{colors::Black};
    Color editorText{colors::White};

    // --- errors ---
    Color errorText{colors::BrightRed};

    // --- syntax tokens in the Text Editor ---
    Color syntaxKeyword{colors::BrightBlue};
    Color syntaxType{colors::BrightCyan};
    Color syntaxString{colors::BrightGreen};
    Color syntaxNumber{colors::BrightMagenta};
    Color syntaxComment{colors::Gray};
    Color syntaxDirective{colors::BrightYellow};
};

// The current theme. Swapping it = a new theme for the entire UI.
inline Theme active{};

// A theme from the catalog - id (stable settings key, e.g. "classic") +
// display name (e.g. "Classic") + full palette of roles.
struct NamedTheme {
    std::string id;
    std::string label;
    Theme theme;
};

// Catalog of themes available in the system (Settings cycles through it with Enter).
const std::vector<NamedTheme>& catalog();

// Theme with the given id, or nullptr (unknown id).
const NamedTheme* find(std::string_view id);

// Replaces `active` with the theme having the given id. Unknown id = no change.
// Returns true if the theme was applied.
bool apply(std::string_view id);

} // namespace fenrir::theme
