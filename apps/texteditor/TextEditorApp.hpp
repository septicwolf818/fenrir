#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "TextEditorView.hpp"

namespace fenrir::apps {

class TextEditorApp : public App {
public:
    std::string name() const override { return "Text Editor"; }
    std::string id() const override { return "text-editor"; }

    std::vector<std::string> fileExtensions() const override {
        return {"txt", "md", "log", "ini", "cfg", "conf", "csv", "json", "xml",
                "yaml", "yml", "c", "h", "cc", "cpp", "hpp", "py", "sh", "html", "css"};
    }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        int n = ++instanceCounter_;
        std::string path = fs::join(fs::homeDir(), "document" + std::to_string(n) + ".txt");
        auto view = std::make_unique<TextEditorView>(ctx, std::move(path));
        TextEditorView* raw = view.get();

        view->setMenuTabs({{U"File", std::vector<MenuItem>{
            {U"New tab (Ctrl+N)", [raw] { raw->newFile(); }},
            {U"Open... (Ctrl+O)", [raw] { raw->openFile(); }},
            {U"Open folder (Ctrl+D)", [raw] { raw->openFolder(); }},
            {U"File panel (Ctrl+B)", [raw] { raw->toggleTreeFocus(); }},
            {U"Save (Ctrl+S)", [raw] { raw->save(); }},
            {U"Save as...", [raw] { raw->saveAs(); }},
            {U"Close tab (Ctrl+T)", [raw] { raw->closeTab(); }},
            {U"Previous tab (Alt+PgUp)", [raw] { raw->prevTab(); }},
            {U"Next tab (Alt+PgDn)", [raw] { raw->nextTab(); }},
        }},
        {U"Format", std::vector<MenuItem>{
            {U"Line wrap (Ctrl+E)", [raw] { raw->toggleWrap(); }},
            {U"Line numbers (Ctrl+G)", [raw] { raw->toggleLineNumbers(); }},
            {U"Tab = spaces (Ctrl+Y)", [raw] { raw->toggleSpaces(); }},
        }}});

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newFile(); });
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openFile(); });
        view->pushShortcut({true, Key::Char, U's'}, [raw] { raw->save(); });
        view->pushShortcut({true, Key::Char, U't'}, [raw] { raw->closeTab(); });
        view->pushShortcut({true, Key::Char, U'g'}, [raw] { raw->toggleLineNumbers(); });
        view->pushShortcut({true, Key::Char, U'e'}, [raw] { raw->toggleWrap(); });
        view->pushShortcut({true, Key::Char, U'y'}, [raw] { raw->toggleSpaces(); });
        view->pushShortcut({true, Key::Char, U'd'}, [raw] { raw->openFolder(); });
        view->pushShortcut({true, Key::Char, U'b'}, [raw] { raw->toggleTreeFocus(); });
        // Alt+PgUp/PgDn = previous/next tab. Bare console only.
        // On Linux: Alt = ESC prefix, so the sequences stay distinguishable
        // (Alt+Tab too, for windows). Ctrl+PgUp/PgDn on the console has IDENTICAL bytes
        // to PgUp/PgDn (text control) — per the kernel keymap.
        view->pushShortcut({false, Key::PageDown, std::nullopt, true}, [raw] { raw->nextTab(); });
        view->pushShortcut({false, Key::PageUp, std::nullopt, true}, [raw] { raw->prevTab(); });

        return view;
    }

    std::unique_ptr<Widget> createFileView(AppContext& ctx, const std::string& path) override {
        return std::make_unique<TextEditorView>(ctx, path);
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(12, desktopArea.w - 4));
        int h = std::min(20, std::max(7, desktopArea.h - 4));
        int cascade = instanceCounter_.load() % 5;
        int x = desktopArea.x + std::min(1 + cascade * 2, std::max(0, desktopArea.w - w));
        int y = desktopArea.y + std::min(cascade, std::max(0, desktopArea.h - h));
        return Rect{x, y, w, h};
    }

private:
    static inline std::atomic<int> instanceCounter_{0};
};

} // namespace fenrir::apps
