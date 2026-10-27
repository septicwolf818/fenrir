#pragma once

#include <memory>
#include <string>

#include "BrowserView.hpp"
#include "fenrir/app/App.hpp"

namespace fenrir::apps {

class BrowserApp final : public App {
public:
    std::string name() const override { return "Browser"; }
    std::string id() const override { return "browser"; }
    std::unique_ptr<Widget> createView(AppContext& context) override;
    Rect preferredBounds(const Rect& desktopArea) const override;
};

} // namespace fenrir::apps
