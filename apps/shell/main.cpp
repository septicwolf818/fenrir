// "den" (wilcza nora) - shell Fenrira. Jedyna odpowiedzialnosc tego pliku:
// zarejestrowac dostepne aplikacje w Desktop i uruchomic petle glowna.
// Zeby dodac wlasna aplikacje: napisz klase implementujaca fenrir::App
// (patrz apps/texteditor/TextEditorApp.hpp i apps/filemanager/FileManagerApp.hpp
// jako przyklady) i dopisz ponizej jedna linijke registerApp(...).

#include <cstdio>

#include "fenrir/desktop/Desktop.hpp"
#include "../texteditor/TextEditorApp.hpp"
#include "../filemanager/FileManagerApp.hpp"
#include "../calculator/CalculatorApp.hpp"

int main() {
    try {
        fenrir::Desktop desktop;
        desktop.setDesktopName("Fenrir");

        desktop.apps().registerApp(std::make_unique<fenrir::apps::TextEditorApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::FileManagerApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::CalculatorApp>());
        // Kolejne aplikacje dopisujesz tutaj.

        return desktop.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Blad: %s\n", e.what());
        return 1;
    }
}
