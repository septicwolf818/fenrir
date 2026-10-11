#pragma once
#include <memory>
#include <vector>

#include "fenrir/app/App.hpp"

namespace fenrir {

// Rejestr zainstalowanych aplikacji. Desktop czyta ta liste, zeby zbudowac
// menu "Aplikacje". Dodanie nowej aplikacji do systemu = registerApp(...),
// bez dotykania kodu Desktop/WindowManager.
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
