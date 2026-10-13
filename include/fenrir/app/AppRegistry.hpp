#pragma once
#include <memory>
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

private:
    std::vector<std::unique_ptr<App>> apps_;
};

} // namespace fenrir
