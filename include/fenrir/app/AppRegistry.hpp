#pragma once
#include <memory>
#include <string_view>
#include <vector>

#include "fenrir/app/App.hpp"

namespace fenrir {

// Registry of installed applications. Desktop reads this list to build the
// "Applications" menu. Adding a new app to the system = registerApp(...),
// without touching Desktop/WindowManager code.
class AppRegistry {
public:
    App* registerApp(std::unique_ptr<App> app) {
        apps_.push_back(std::move(app));
        return apps_.back().get();
    }

    const std::vector<std::unique_ptr<App>>& apps() const { return apps_; }

    App* findById(std::string_view id) const {
        for (const auto& app : apps_)
            if (app->id() == id) return app.get();
        return nullptr;
    }

    std::vector<App*> fileApps() const {
        std::vector<App*> result;
        for (const auto& app : apps_)
            if (!app->fileExtensions().empty()) result.push_back(app.get());
        return result;
    }

private:
    std::vector<std::unique_ptr<App>> apps_;
};

} // namespace fenrir
