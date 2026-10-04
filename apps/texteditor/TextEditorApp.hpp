#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/widgets/TextArea.hpp"

namespace fenrir::apps {

// Przykladowa aplikacja: implementuje TYLKO fenrir::App. Nie zna Terminala,
// Screena, WindowManagera ani petli glownej - to wszystko dostarcza Desktop.
// Ctrl+O/Ctrl+S w tej aplikacji NIE otwieraja wlasnego, recznie napisanego
// dialogu - wolaja AppContext (ctx.openFile/ctx.saveFile), czyli DOKLADNIE
// to samo API, ktorego uzywa FileManagerApp. To jest ten "framework
// aplikacji": gotowe klocki (dialogi plikow) zamiast pisania ich od nowa
// w kazdej aplikacji.
class TextEditorApp : public App {
public:
    std::string name() const override { return "Edytor tekstu"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto area = std::make_unique<TextArea>();
        int n = ++instanceCounter_;
        std::string path = fs::join(fs::homeDir(), "dokument" + std::to_string(n) + ".txt");
        area->loadFile(path);

        TextArea* raw = area.get();

        area->onOpenRequested = [&ctx, raw]() {
            std::string startDir = raw->path().empty() ? fs::homeDir() : fs::parent(raw->path());
            ctx.openFile(startDir, [raw](const std::string& picked) { raw->loadFile(picked); });
        };

        area->onSaveRequested = [&ctx, raw]() {
            if (!raw->path().empty()) {
                raw->saveToCurrentPath();
                return;
            }
            ctx.saveFile(fs::homeDir(), "dokument.txt",
                         [raw](const std::string& picked) { raw->saveFile(picked); });
        };

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
    // inline (C++17) zeby ta klasa mogla zostac w calosci w naglowku;
    // atomic wylacznie po to, zeby preferredBounds() (const) mogl je czytac
    // bez castowania const-away - w praktyce apka dziala jednowatkowo.
    static inline std::atomic<int> instanceCounter_{0};
};

} // namespace fenrir::apps
