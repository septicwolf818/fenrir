#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/core/Notifications.hpp"
#include "fenrir/core/Settings.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/core/Wallpaper.hpp"
#include "fenrir/widgets/ListView.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/TabBar.hpp"

namespace fenrir::apps {

// Settings view: tabbed control panel. The TabBar selects a category; the
// ListView on the left lists the settings of that category (current value as
// the detail column on the right); Enter/Space on a row changes the value.
class SettingsView : public Widget {
public:
    explicit SettingsView(AppContext& ctx) : ctx_(ctx) {
        tabs_.setTabs({
            {U"Desktop"},
            {U"Look & Feel"},
            {U"Notifications"},
            {U"Background"},
        });
        tabs_.onSwitch = [this](int idx) { currentTab_ = idx; rebuild(); };
        tabs_.select(0);

        list_.setHeader(U"Settings");
        list_.onActivate = [this](int idx) { activateRow(idx); };
        rebuild();
    }

    void prevTab() {
        if (tabs_.count() == 0) return;
        int next = (tabs_.selected() - 1 + tabs_.count()) % tabs_.count();
        tabs_.select(next);
        if (tabs_.onSwitch) tabs_.onSwitch(next);
    }
    void nextTab() {
        if (tabs_.count() == 0) return;
        int next = (tabs_.selected() + 1) % tabs_.count();
        tabs_.select(next);
        if (tabs_.onSwitch) tabs_.onSwitch(next);
    }

    void draw(Screen& screen) override {
        const int tabH = 1;
        const int statusH = 1;
        tabs_.setBounds(Rect{bounds_.x, bounds_.y, bounds_.w, tabH});
        tabs_.setFocused(focused_);
        tabs_.draw(screen);

        list_.setBounds(Rect{bounds_.x, bounds_.y + tabH, bounds_.w,
                            std::max(0, bounds_.h - tabH - statusH)});
        list_.setFocused(focused_);
        list_.draw(screen);

        status_.setBounds(Rect{bounds_.x, bounds_.y + std::max(0, bounds_.h - statusH), bounds_.w, statusH});
        status_.setText(U"Tab: next tab | Arrows: move | Enter/Space: change");
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.key == Key::Tab && !ev.ctrl && !ev.alt) {
            int next = (tabs_.selected() + 1) % std::max(1, tabs_.count());
            tabs_.select(next);
            if (tabs_.onSwitch) tabs_.onSwitch(next);
            return KeyEventResult::Consumed;
        }
        // Let the tab bar handle Left/Right when it has focus-ish; otherwise list.
        if (ev.key == Key::Left || ev.key == Key::Right) {
            int delta = (ev.key == Key::Right) ? 1 : -1;
            int next = (tabs_.selected() + delta + tabs_.count()) % std::max(1, tabs_.count());
            tabs_.select(next);
            if (tabs_.onSwitch) tabs_.onSwitch(next);
            return KeyEventResult::Consumed;
        }
        return list_.handleKey(ev);
    }

    bool editable() const override { return false; }

    int minWidth() const override { return 40; }
    int minHeight() const override { return 12; }

private:
    void showStatus(std::u32string message) {
        status_.setMessage(std::nullopt, std::optional<std::u32string>{std::move(message)});
    }

    void rebuild() {
        std::vector<ListView::Item> items;
        switch (currentTab_) {
            case 0: { // Desktop
                items.push_back({U"Desktop name", text::toUtf32(settings::active.desktopName)});
                items.push_back({U"Taskbar clock seconds", settings::active.taskbarSeconds ? U"On" : U"Off"});
                items.push_back({U"Cursor blink", settings::active.cursorBlink ? U"On" : U"Off"});
                break;
            }
            case 1: { // Look & Feel
                items.push_back({U"Theme", text::toUtf32(currentThemeLabel())});
                const wallpaper::Wallpaper& wp = settings::active.wallpaper;
                const bool pattern = wp.mode == wallpaper::Wallpaper::Mode::Pattern;
                items.push_back({U"Wallpaper", wp.enabled ? U"On" : U"Off"});
                items.push_back({U"Wallpaper style", text::toUtf32(pattern ? "Pattern" : "Solid"), false, wp.enabled});
                items.push_back({U"Background color", text::toUtf32(wallpaper::colorName(wp.bg)), false, wp.enabled});
                items.push_back({U"Pattern color", text::toUtf32(wallpaper::colorName(wp.fg)), false, wp.enabled && pattern});
                items.push_back({U"Pattern character", std::u32string(1, wp.glyph), false, wp.enabled && pattern});
                break;
            }
            case 2: { // Notifications
                items.push_back({U"App notifications", settings::active.appNotifications ? U"On" : U"Off"});
                break;
            }
            case 3: { // Background
                const auto names = ctx_.backgroundWorkers();
                if (names.empty()) {
                    items.push_back({U"No background services", U""});
                } else {
                    for (const auto& n : names) items.push_back({text::toUtf32(n), U"running"});
                }
                break;
            }
            default: break;
        }
        list_.setItems(std::move(items));
    }

    std::string currentThemeLabel() const {
        const theme::NamedTheme* n = theme::find(settings::active.theme);
        return n ? n->label : settings::active.theme;
    }

    void activateRow(int idx) {
        bool ok = false;
        if (currentTab_ == 0) {
            switch (idx) {
                case 0: editDesktopName(); return;
                case 1:
                    settings::active.taskbarSeconds = !settings::active.taskbarSeconds;
                    ok = saveChecked("taskbar_seconds", settings::active.taskbarSeconds ? "1" : "0");
                    break;
                case 2:
                    settings::active.cursorBlink = !settings::active.cursorBlink;
                    ok = saveChecked("cursor_blink", settings::active.cursorBlink ? "1" : "0");
                    break;
                default: return;
            }
        } else if (currentTab_ == 1) {
            const wallpaper::Wallpaper& wp = settings::active.wallpaper;
            (void)wp;
            switch (idx) {
                case 0: ok = cycleTheme(); break;
                case 1:
                    settings::active.wallpaper.enabled = !settings::active.wallpaper.enabled;
                    ok = saveChecked("wallpaper_enabled", settings::active.wallpaper.enabled ? "1" : "0");
                    break;
                case 2:
                    settings::active.wallpaper.mode =
                        (settings::active.wallpaper.mode == wallpaper::Wallpaper::Mode::Pattern)
                            ? wallpaper::Wallpaper::Mode::Solid
                            : wallpaper::Wallpaper::Mode::Pattern;
                    ok = saveChecked("wallpaper_mode",
                                     settings::active.wallpaper.mode == wallpaper::Wallpaper::Mode::Pattern
                                         ? "pattern" : "solid");
                    break;
                case 3:
                    settings::active.wallpaper.bg =
                        static_cast<uint8_t>((settings::active.wallpaper.bg + 1) & 15);
                    ok = saveChecked("wallpaper_bg", std::to_string(settings::active.wallpaper.bg));
                    break;
                case 4:
                    settings::active.wallpaper.fg =
                        static_cast<uint8_t>((settings::active.wallpaper.fg + 1) & 15);
                    ok = saveChecked("wallpaper_fg", std::to_string(settings::active.wallpaper.fg));
                    break;
                case 5: ok = cycleWallpaperGlyph(); break;
                default: return;
            }
        } else if (currentTab_ == 2) {
            if (idx == 0) {
                settings::active.appNotifications = !settings::active.appNotifications;
                ok = saveChecked("app_notifications", settings::active.appNotifications ? "1" : "0");
                notifications::global().setEnabled(settings::active.appNotifications);
            } else return;
        } else {
            return; // Background tab is read-only.
        }
        rebuild();
        showStatus(ok ? U"Saved" : U"Not saved");
    }

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

    bool cycleWallpaperGlyph() {
        char32_t next = wallpaper::glyphPresets[0];
        for (std::size_t i = 0; i < wallpaper::glyphPresetCount; ++i) {
            if (wallpaper::glyphPresets[i] == settings::active.wallpaper.glyph) {
                next = wallpaper::glyphPresets[(i + 1) % wallpaper::glyphPresetCount];
                break;
            }
        }
        settings::active.wallpaper.glyph = next;
        return saveChecked("wallpaper_glyph", text::toUtf8(std::u32string(1, next)));
    }

    bool saveChecked(const char* key, const std::string& value) {
        (void)key; (void)value;
        settings::markDirty();
        settings::save();
        return true;
    }

    void editDesktopName() {
        std::weak_ptr<int> token = lifetimeToken();
        ctx_.prompt(U"Desktop name", U"Enter a new desktop name:", U"Name: ",
                    text::toUtf32(settings::active.desktopName),
                    [this, token](std::optional<std::u32string> v) {
                        if (!token.lock()) return;
                        if (!v) return;
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
    int currentTab_ = 0;
    TabBar tabs_;
    ListView list_;
    StatusBar status_;
};

class SettingsApp : public App {
public:
    std::string name() const override { return "Settings"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<SettingsView>(ctx);
        SettingsView* raw = view.get();
        addExitMenu(*view, ctx);
        addAboutMenu(*view, ctx, U"Settings",
                     U"Configure the desktop, look and feel, notifications, and background services.");
        view->pushShortcut({false, Key::F3, std::nullopt, false}, [raw] { raw->prevTab(); });
        view->pushShortcut({false, Key::F4, std::nullopt, false}, [raw] { raw->nextTab(); });
        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(40, desktopArea.w - 4));
        int h = std::min(18, std::max(14, desktopArea.h - 4));
        return Rect{desktopArea.x + std::max(0, (desktopArea.w - w) / 2),
                    desktopArea.y + std::max(0, (desktopArea.h - h) / 2), w, h};
    }
};

} // namespace fenrir::apps
