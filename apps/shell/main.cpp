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
#include "../settings/SettingsApp.hpp"
#include "../calendar/CalendarApp.hpp"
#include "../paint/PaintApp.hpp"
#include "../terminal/TerminalApp.hpp"

int main() {
    try {
        fenrir::Desktop desktop;
        // Nazwa pulpitu: ustawienia z ~/.config/fenrir/settings.ini (ladowane
        // w konstruktorze Desktop). Zapis domyslny — fallback gdyby plik nie
        // istnial lub nazwa byla pusta.
        desktop.setDesktopName("Fenrir");

        desktop.apps().registerApp(std::make_unique<fenrir::apps::TextEditorApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::FileManagerApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::CalculatorApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::SettingsApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::CalendarApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::PaintApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::TerminalApp>());
        // Kolejne aplikacje dopisujesz tutaj.

        return desktop.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Blad: %s\n", e.what());
        return 1;
    }
}
