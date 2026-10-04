#pragma once
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/widgets/FileManagerView.hpp"

namespace fenrir::apps {

class FileManagerApp : public App {
public:
    std::string name() const override { return "Menedzer plikow"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        return std::make_unique<FileManagerView>(ctx, fs::homeDir());
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(50, std::max(24, desktopArea.w - 6));
        int h = std::min(18, std::max(8, desktopArea.h - 4));
        return Rect{desktopArea.x + 3, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir::apps
