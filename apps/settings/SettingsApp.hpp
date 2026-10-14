#pragma once
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/core/Settings.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/ListView.hpp"
#include "fenrir/widgets/StatusBar.hpp"

namespace fenrir::apps {

// Settings view: a list of rows (name + current value as detail),
// Enter/Space on a row changes the value. Theme is swapped immediately
// (theme::apply — the whole UI draws from theme::active every tick, so the
// change is visible at once), every change is saved to settings.ini.
class SettingsView : public Widget {
public:
    explicit SettingsView(AppContext& ctx) : ctx_(ctx) {
        list_.setHeader(U"Settings");
        list_.onActivate = [this](int idx) { activateRow(idx); };
        rebuild();
    }

    void draw(Screen& screen) override {
        const int statusH = 1;
        list_.setBounds(Rect{bounds_.x, bounds_.y, bounds_.w, std::max(0, bounds_.h - statusH)});
        list_.setFocused(focused_);
        list_.draw(screen);

        status_.setBounds(Rect{bounds_.x, bounds_.y + std::max(0, bounds_.h - statusH), bounds_.w, statusH});
        status_.setText(U"Arrows: move | Enter/Space: change");
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        return list_.handleKey(ev);
    }

    bool editable() const override { return false; }

    // Minimum: header + settings rows + status bar.
    int minWidth() const override { return 30; }
    int minHeight() const override { return 6; }

private:
    void showStatus(std::u32string message) {
        status_.setMessage(std::nullopt, std::optional<std::u32string>{std::move(message)});
    }

    void rebuild() {
        std::vector<ListView::Item> items;
        items.push_back({U"Theme", text::toUtf32(currentThemeLabel())});
        items.push_back({U"Taskbar clock seconds", settings::active.taskbarSeconds ? U"On" : U"Off"});
        items.push_back({U"Desktop name", text::toUtf32(settings::active.desktopName)});
        items.push_back({U"Cursor blink", settings::active.cursorBlink ? U"On" : U"Off"});
        list_.setItems(std::move(items));
    }

    std::string currentThemeLabel() const {
        const theme::NamedTheme* n = theme::find(settings::active.theme);
        return n ? n->label : settings::active.theme;
    }

    void activateRow(int idx) {
        bool ok = false; // whether settings.ini actually accepted the new value
        switch (idx) {
            case 0: ok = cycleTheme(); break;
            case 1:
                settings::active.taskbarSeconds = !settings::active.taskbarSeconds;
                ok = saveChecked("taskbar_seconds", settings::active.taskbarSeconds ? "1" : "0");
                break;
            case 2: editDesktopName(); return; // the prompt has its own callback + rebuild
            case 3:
                settings::active.cursorBlink = !settings::active.cursorBlink;
                ok = saveChecked("cursor_blink", settings::active.cursorBlink ? "1" : "0");
                break;
            default: return;
        }
        rebuild();
        showStatus(ok ? U"Saved" : U"Not saved");
    }

    // Returns false when there's nothing to cycle (empty catalog) or when the file
    // didn't accept the new value — then the status shows "Not saved".
    bool cycleTheme() {
        const auto& cat = theme::catalog();
        if (cat.empty()) return false;
        std::size_t nextIndex = 0;
        for (std::size_t i = 0; i < cat.size(); ++i) {
            if (cat[i].id == settings::active.theme) {
                nextIndex = (i + 1) % cat.size();
                break;
            }
        }
        const theme::NamedTheme& next = cat[nextIndex];
        theme::apply(next.id);
        settings::active.theme = next.id;
        return saveChecked("theme", settings::active.theme);
    }

    // settings::save() returns void and stays silent when the file can't be written
    // (e.g. no permission for ~/.config/fenrir) — so we read settings.ini ourselves
    // and compare against the value the UI shows. Only then do we report "Saved",
    // otherwise the status bar would lie.
    bool saveChecked(const char* key, const std::string& value) {
        settings::save();
        const std::string file = settings::path(); // local copy, as in settings::load()
        std::ifstream f(file);
        if (!f) return false;
        const std::string want = std::string(key) + "=" + value;
        std::string line;
        while (std::getline(f, line))
            if (line == want) return true;
        return false;
    }

    void editDesktopName() {
        // The dialog lives in its own window; the token guards `this` — if the
        // settings window closes first, the callback just returns.
        std::weak_ptr<int> token = lifetimeToken();
        ctx_.prompt(U"Desktop name", U"Enter a new desktop name:", U"Name: ",
                    text::toUtf32(settings::active.desktopName),
                    [this, token](std::optional<std::u32string> v) {
                        if (!token.lock()) return;
                        if (!v) return;
                        // The settings loader trims ASCII whitespace around values,
                        // so trim the same characters here to preserve the name on reload.
                        const std::size_t first = v->find_first_not_of(U" \t\r\n");
                        if (first == std::u32string::npos) {
                            showStatus(U"Enter a name");
                            return;
                        }
                        const std::size_t last = v->find_last_not_of(U" \t\r\n");
                        const std::u32string name = v->substr(first, last - first + 1);
                        settings::active.desktopName = text::toUtf8(name);
                        rebuild();
                        showStatus(saveChecked("desktop_name", settings::active.desktopName)
                                       ? U"Saved" : U"Not saved");
                    });
    }

    AppContext& ctx_;
    ListView list_;
    StatusBar status_;
};

class SettingsApp : public App {
public:
    std::string name() const override { return "Settings"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<SettingsView>(ctx);
        addAboutMenu(*view, ctx, U"Settings",
                     U"Choose the desktop theme, clock, cursor, and editor preferences.");
        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(52, std::max(32, desktopArea.w - 4));
        int h = std::min(10, std::max(8, desktopArea.h - 4));
        return Rect{desktopArea.x + std::max(0, (desktopArea.w - w) / 2),
                    desktopArea.y + std::max(0, (desktopArea.h - h) / 2), w, h};
    }
};

} // namespace fenrir::apps
