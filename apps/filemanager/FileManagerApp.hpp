#pragma once
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "FileManagerView.hpp"

namespace fenrir::apps {

class FileManagerApp : public App {
public:
    std::string name() const override { return "Menedzer plikow"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<FileManagerView>(ctx, fs::homeDir());
        FileManagerView* raw = view.get();

        // Menu: operacje na panelu (te same metody co skróty).
        // Model dwupanelowy: Ctrl+C/X kopiuja/przenosza DO DRUGIEGO panelu
        // (aktywny = zrodlo, drugi = cel) — bez schowka i wklejania.
        view->setMenuTabs({{U"Plik", std::vector<MenuItem>{
            {U"Idz do folderu... (Ctrl+G)", [raw] { raw->goToFolder(); }},
            {U"Odswiez", [raw] { raw->refreshBoth(); }},
        }},
        {U"Edycja", std::vector<MenuItem>{
            {U"Kopiuj do drugiego panelu (Ctrl+C)", [raw] { raw->copySelected(); }},
            {U"Przenies do drugiego panelu (Ctrl+X)", [raw] { raw->moveSelected(); }},
            {U"Przemianuj (Ctrl+R)", [raw] { raw->renameSelected(); }},
            {U"Usun (Del)", [raw] { raw->deleteSelected(); }},
        }},
        {U"Nawigacja", std::vector<MenuItem>{
            {U"W gore (Backspace)", [raw] { raw->goUp(); }},
        }}});

        // Skróty: rejestrowane na widgetcie — Desktop sprawdza je przed handleKey.
        view->pushShortcut({true, Key::Char, U'g'}, [raw] { raw->goToFolder(); });
        view->pushShortcut({true, Key::Char, U'c'}, [raw] { raw->copySelected(); });
        view->pushShortcut({true, Key::Char, U'x'}, [raw] { raw->moveSelected(); });
        view->pushShortcut({true, Key::Char, U'r'}, [raw] { raw->renameSelected(); });
        view->pushShortcut({false, Key::Delete}, [raw] { raw->deleteSelected(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(30, desktopArea.w - 6));
        int h = std::min(20, std::max(8, desktopArea.h - 4));
        return Rect{desktopArea.x + 2, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir::apps
