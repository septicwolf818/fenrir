#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fenrir/app/App.hpp"
#include "TextEditorView.hpp"

namespace fenrir::apps {

class TextEditorApp : public App {
public:
    std::string name() const override { return "Text Editor"; }
    std::string id() const override { return "text-editor"; }

    std::vector<std::string> fileExtensions() const override {
        return {"txt", "md", "log", "ini", "cfg", "conf", "csv", "json", "xml",
                "markdown", "yaml", "yml", "toml", "properties", "cmake", "c", "h", "cc",
                "cpp", "cxx", "hpp", "hxx", "js", "mjs", "cjs", "ts", "tsx", "java",
                "go", "rs", "py", "sh", "bash", "zsh", "html", "htm", "css"};
    }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        ++instanceCounter_;
        auto view = std::make_unique<TextEditorView>(ctx, std::string{});
        TextEditorView* raw = view.get();

        view->setMenuTabs({{U"File", std::vector<MenuItem>{
            {U"New tab (Ctrl+N)", [raw] { raw->newFile(); }},
            {U"Open... (Ctrl+O)", [raw] { raw->openFile(); }},
            {U"Open folder (Ctrl+D)", [raw] { raw->openFolder(); }},
            {U"File panel (Ctrl+B)", [raw] { raw->toggleTreeFocus(); }},
            {U"Save (Ctrl+S)", [raw] { raw->save(); }},
            {U"Save as...", [raw] { raw->saveAs(); }},
            {U"Close tab (Ctrl+T)", [raw] { raw->closeTab(); }},
            {U"Previous tab (F3)", [raw] { raw->prevTab(); }},
            {U"Next tab (F4)", [raw] { raw->nextTab(); }},
        }},
        {U"Format", std::vector<MenuItem>{
            {U"Line wrap (Ctrl+E)", [raw] { raw->toggleWrap(); }},
            {U"Line numbers (Ctrl+G)", [raw] { raw->toggleLineNumbers(); }},
            {U"Tab = spaces (Ctrl+Y)", [raw] { raw->toggleSpaces(); }},
            {U"Toggle syntax highlighting", [raw] { raw->toggleSyntaxHighlighting(); }},
        }}});
        addAboutMenu(*view, ctx, U"Text Editor",
                     U"Edit plain-text files with tabs, a file tree, and optional syntax highlighting.");

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newFile(); });
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openFile(); });
        view->pushShortcut({true, Key::Char, U's'}, [raw] { raw->save(); });
        view->pushShortcut({true, Key::Char, U't'}, [raw] { raw->closeTab(); });
        view->pushShortcut({true, Key::Char, U'g'}, [raw] { raw->toggleLineNumbers(); });
        view->pushShortcut({true, Key::Char, U'e'}, [raw] { raw->toggleWrap(); });
        view->pushShortcut({true, Key::Char, U'y'}, [raw] { raw->toggleSpaces(); });
        view->pushShortcut({true, Key::Char, U'd'}, [raw] { raw->openFolder(); });
        view->pushShortcut({true, Key::Char, U'b'}, [raw] { raw->toggleTreeFocus(); });
        // F3/F4 = previous/next tab. Plain function keys reach the app on
        // every console and emulator, unlike Alt+PgUp/PgDn (eaten or sent
        // without the Alt bit by some keymaps) and Alt+Left/Right (the
        // Linux console grabs those for VT switching before we see them).
        view->pushShortcut({false, Key::F3, std::nullopt, false}, [raw] { raw->prevTab(); });
        view->pushShortcut({false, Key::F4, std::nullopt, false}, [raw] { raw->nextTab(); });
        // Alt+1..Alt+9 = go directly to tab N. A single ESC + digit on every
        // console and emulator, so it keeps working where the keymap eats or
        // rewrites the PgUp/PgDn combinations (e.g. console scrollback).
        for (char32_t d = U'1'; d <= U'9'; ++d) {
            view->pushShortcut({false, Key::Char, d, true},
                               [raw, idx = static_cast<int>(d - U'1')] { raw->goToTab(idx); });
        }

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
