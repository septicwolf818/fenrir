#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "TextEditorView.hpp"

namespace fenrir::apps {

class TextEditorApp : public App {
public:
    std::string name() const override { return "Edytor tekstu"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        int n = ++instanceCounter_;
        std::string path = fs::join(fs::homeDir(), "dokument" + std::to_string(n) + ".txt");
        auto view = std::make_unique<TextEditorView>(ctx, std::move(path));
        TextEditorView* raw = view.get();

        view->setMenuTabs({{U"Plik", std::vector<MenuItem>{
            {U"Nowy (Ctrl+N)", [raw] { raw->newFile(); }},
            {U"Otworz... (Ctrl+O)", [raw] { raw->openFile(); }},
            {U"Zapisz (Ctrl+S)", [raw] { raw->save(); }},
            {U"Zapisz jako...", [raw] { raw->saveAs(); }},
        }},
        {U"Format", std::vector<MenuItem>{
            {U"Zawijanie linii (Ctrl+E)", [raw] { raw->toggleWrap(); }},
            {U"Numery linii (Ctrl+G)", [raw] { raw->toggleLineNumbers(); }},
            {U"Tab = spacje (Ctrl+Y)", [raw] { raw->toggleSpaces(); }},
        }}});

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newFile(); });
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openFile(); });
        view->pushShortcut({true, Key::Char, U's'}, [raw] { raw->save(); });
        view->pushShortcut({true, Key::Char, U'g'}, [raw] { raw->toggleLineNumbers(); });
        view->pushShortcut({true, Key::Char, U'y'}, [raw] { raw->toggleSpaces(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(20, desktopArea.w - 4));
        int h = std::min(20, std::max(6, desktopArea.h - 4));
        int cascade = instanceCounter_.load() % 5;
        int x = desktopArea.x + 1 + cascade * 2;
        int y = desktopArea.y + cascade;
        return Rect{x, y, w, h};
    }

private:
    static inline std::atomic<int> instanceCounter_{0};
};

} // namespace fenrir::apps
