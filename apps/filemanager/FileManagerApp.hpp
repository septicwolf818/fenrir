#pragma once
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "FileManagerView.hpp"

namespace fenrir::apps {

class FileManagerApp : public App {
public:
    std::string name() const override { return "File Manager"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<FileManagerView>(ctx, fs::homeDir());
        FileManagerView* raw = view.get();

        // Menu: operations on the panel (same methods as the shortcuts).
        // Dual-panel model: Ctrl+C/X copy/move TO THE OTHER panel
        // (active = source, other = target) — no clipboard, no pasting.
        view->setMenuTabs({{U"File", std::vector<MenuItem>{
            {U"Open with... (Ctrl+O)", [raw] { raw->openWithSelected(); }},
            {U"Go to folder... (Ctrl+G)", [raw] { raw->goToFolder(); }},
            {U"Refresh", [raw] { raw->refreshBoth(); }},
        }},
        {U"Edit", std::vector<MenuItem>{
            {U"Copy to other panel (Ctrl+C)", [raw] { raw->copySelected(); }},
            {U"Move to other panel (Ctrl+X)", [raw] { raw->moveSelected(); }},
            {U"Rename (Ctrl+R)", [raw] { raw->renameSelected(); }},
            {U"Delete (Del)", [raw] { raw->deleteSelected(); }},
        }},
        {U"Navigation", std::vector<MenuItem>{
            {U"Up (Backspace)", [raw] { raw->goUp(); }},
        }}});
        addAboutMenu(*view, ctx, U"File Manager",
                     U"Browse, copy, move, rename, and open files in a dual-panel workspace.");

        // Shortcuts: registered on the widget — Desktop checks them before handleKey.
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openWithSelected(); });
        view->pushShortcut({true, Key::Char, U'g'}, [raw] { raw->goToFolder(); });
        view->pushShortcut({true, Key::Char, U'c'}, [raw] { raw->copySelected(); });
        view->pushShortcut({true, Key::Char, U'x'}, [raw] { raw->moveSelected(); });
        view->pushShortcut({true, Key::Char, U'r'}, [raw] { raw->renameSelected(); });
        view->pushShortcut({false, Key::Delete}, [raw] { raw->deleteSelected(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(20, desktopArea.w - 4));
        int h = std::min(20, std::max(6, desktopArea.h - 4));
        const int x = desktopArea.x + std::min(2, std::max(0, desktopArea.w - w));
        const int y = desktopArea.y + std::min(1, std::max(0, desktopArea.h - h));
        return Rect{x, y, w, h};
    }
};

} // namespace fenrir::apps
