#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/widgets/TextArea.hpp"

namespace fenrir::apps {

class TextEditorApp : public App {
public:
    std::string name() const override { return "Edytor tekstu"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto area = std::make_unique<TextArea>();
        int n = ++instanceCounter_;
        std::string path = fs::join(fs::homeDir(), "dokument" + std::to_string(n) + ".txt");
        area->loadFile(path);

        TextArea* raw = area.get();

        // --- Utworzenie nowego pliku (Ctrl+N) ------------------------------------
        auto newCb = [raw]() {
            raw->newFile();
        };

        // --- Otworz plik (Ctrl+O) -----------------------------------------------
        auto openCb = [&ctx, raw]() {
            std::string startDir = raw->path().empty() ? fs::homeDir() : fs::parent(raw->path());
            ctx.openFile(startDir, [raw](const std::string& picked) { raw->loadFile(picked); });
        };

        // --- Zapisz (Ctrl+S) - zapis do obecnej sciezki lub "Zapisz jako" --------
        auto saveCb = [&ctx, raw]() {
            if (!raw->path().empty()) { raw->saveToCurrentPath(); return; }
            ctx.saveFile(fs::homeDir(), "dokument.txt",
                         [raw](const std::string& picked) { raw->saveFile(picked); });
        };

        // --- Zapisz jako (Ctrl+Shift+S) -----------------------------------------
        auto saveAsCb = [&ctx, raw]() {
            std::string startDir = raw->path().empty() ? fs::homeDir() : fs::parent(raw->path());
            ctx.saveFile(startDir, "dokument.txt",
                         [raw](const std::string& picked) { raw->saveFile(picked); });
        };

        // --- Ustaw menu na widgete ----------------------------------------------
        raw->setMenuTabs({{U"Plik", std::vector<MenuItem>{
            {U"Nowy...", newCb},
            {U"Otworz...", openCb},
            {U"Zapisz", saveCb},
            {U"Zapisz jako...", saveAsCb},
        }}});

        // --- Skróty klawiszowe --------------------------------------------------
        KeyShortcut scNew{};
        scNew.ctrl = true;
        scNew.key = Key::Char;
        scNew.character = static_cast<char32_t>(U'N');
        raw->pushShortcut(scNew, newCb);

        KeyShortcut scOpen{};
        scOpen.ctrl = true;
        scOpen.key = Key::Char;
        scOpen.character = static_cast<char32_t>(U'O');
        raw->pushShortcut(scOpen, openCb);

        KeyShortcut scSave{};
        scSave.ctrl = true;
        scSave.key = Key::Char;
        scSave.character = static_cast<char32_t>(U'S');
        raw->pushShortcut(scSave, saveCb);

        KeyShortcut scSaveAs{};
        scSaveAs.ctrl = true;
        scSaveAs.key = Key::Char;
        scSaveAs.character = static_cast<char32_t>(U'S');
        // Uwaga: Ctrl+S i Ctrl+Shift+S rozrozniamy w handleKey Desktopa
        // na razie oba wywoluja saveCb (wystarczajace dla v1).

        return area;
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
