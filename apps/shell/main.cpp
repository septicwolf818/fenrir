// "den" (wolf's den) — Fenrir's shell. This file is only responsible for
// registering available apps with the Desktop and starting the main loop.
// To add your own app, write a class implementing fenrir::App
// (see apps/texteditor/TextEditorApp.hpp and apps/filemanager/FileManagerApp.hpp
// for examples) and add one registerApp(...) line below.

#include <cstdio>

#include "fenrir/desktop/Desktop.hpp"
#include "../texteditor/TextEditorApp.hpp"
#include "../filemanager/FileManagerApp.hpp"
#include "../calculator/CalculatorApp.hpp"
#include "../settings/SettingsApp.hpp"
#include "../calendar/CalendarApp.hpp"
#include "../paint/PaintApp.hpp"
#include "../terminal/TerminalApp.hpp"
#include "../photoviewer/PhotoViewerApp.hpp"
#include "../handbook/HandbookApp.hpp"

int main() {
    try {
        fenrir::Desktop desktop;
        // Desktop name: settings from ~/.config/fenrir/settings.ini (loaded
        // by the Desktop constructor). Default value, used if the file does not
        // exist or the name is empty.
        desktop.setDesktopName("Fenrir");

        desktop.apps().registerApp(std::make_unique<fenrir::apps::TextEditorApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::FileManagerApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::CalculatorApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::SettingsApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::CalendarApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::PaintApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::TerminalApp>());
        desktop.apps().registerApp(std::make_unique<fenrir::apps::PhotoViewerApp>());
        // The Handbook is the built-in documentation (available in F1).
        desktop.apps().registerApp(std::make_unique<fenrir::apps::HandbookApp>());
        // Add more apps here.

        return desktop.run();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
