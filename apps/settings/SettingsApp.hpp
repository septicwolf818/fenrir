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

// Widok ustawien: lista wierszy (nazwa + biezaca wartosc jako detail),
// Enter na wierszu zmienia wartosc. Motyw podmieniany natychmiast
// (theme::apply — caly UI rysuje z theme::active co tick, wiec zmiana
// widoczna od razu), kazda zmiana zapisywana do settings.ini.
class SettingsView : public Widget {
public:
    explicit SettingsView(AppContext& ctx) : ctx_(ctx) {
        list_.setHeader(U"Ustawienia");
        list_.onActivate = [this](int idx) { activateRow(idx); };
        rebuild();
    }

    void draw(Screen& screen) override {
        const int statusH = 1;
        list_.setBounds(Rect{bounds_.x, bounds_.y, bounds_.w, std::max(0, bounds_.h - statusH)});
        list_.setFocused(focused_);
        list_.draw(screen);

        status_.setBounds(Rect{bounds_.x, bounds_.y + std::max(0, bounds_.h - statusH), bounds_.w, statusH});
        // StatusBar rysuje prawa kolumne NA koncu paska, wiec w waskim oknie
        // "Zapisano" nachodziloby na dluga podpowiedz — przy wiadomosci po prawej
        // pokazujemy krotsza instrukcje (12 + 12 <= minWidth 30, nic sie nie naklada).
        status_.setText(statusRight_.empty() ? U"Enter: zmien | Zmiany zapisuja sie od razu"
                                             : U"Enter: zmien",
                        statusRight_);
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        return list_.handleKey(ev);
    }

    bool editable() const override { return false; }

    // Minimum: naglowek + wiersze ustawien + pasek statusu.
    int minWidth() const override { return 30; }
    int minHeight() const override { return 6; }

private:
    void rebuild() {
        std::vector<ListView::Item> items;
        items.push_back({U"Motyw", text::toUtf32(currentThemeLabel())});
        items.push_back({U"Zegar z sekundami", settings::active.taskbarSeconds ? U"Wlaczony" : U"Wylaczony"});
        items.push_back({U"Nazwa pulpitu", text::toUtf32(settings::active.desktopName)});
        items.push_back({U"Miganie kursora", settings::active.cursorBlink ? U"Wlaczony" : U"Wylaczony"});
        list_.setItems(std::move(items));
    }

    std::string currentThemeLabel() const {
        const theme::NamedTheme* n = theme::find(settings::active.theme);
        return n ? n->label : settings::active.theme;
    }

    void activateRow(int idx) {
        bool ok = false; // czy settings.ini faktycznie przyjal nowa wartosc
        switch (idx) {
            case 0: ok = cycleTheme(); break;
            case 1:
                settings::active.taskbarSeconds = !settings::active.taskbarSeconds;
                ok = saveChecked("taskbar_seconds", settings::active.taskbarSeconds ? "1" : "0");
                break;
            case 2: editDesktopName(); return; // prompt ma wlasny callback + rebuild
            case 3:
                settings::active.cursorBlink = !settings::active.cursorBlink;
                ok = saveChecked("cursor_blink", settings::active.cursorBlink ? "1" : "0");
                break;
            default: return;
        }
        rebuild();
        statusRight_ = ok ? U"Zapisano" : U"Nie zapisano";
    }

    // Zwraca false, gdy nie ma czego przelaczyc (pusty katalog) albo gdy plik
    // nie przyjal nowej wartosci — wtedy status pokazuje "Nie zapisano".
    bool cycleTheme() {
        const auto& cat = theme::catalog();
        if (cat.empty()) return false;
        std::size_t cur = 0;
        for (std::size_t i = 0; i < cat.size(); ++i) {
            if (cat[i].id == settings::active.theme) { cur = i; break; }
        }
        const theme::NamedTheme& next = cat[(cur + 1) % cat.size()];
        theme::apply(next.id);
        settings::active.theme = next.id;
        return saveChecked("theme", settings::active.theme);
    }

    // settings::save() zwraca void i milczy, gdy pliku nie da sie zapisac
    // (np. brak prawa do ~/.config/fenrir) — wiec sami odczytujemy settings.ini
    // i porownujemy z wartoscia, ktora pokazuje UI. Tylko wtedy wolamy "Zapisano",
    // inaczej pasek statusu klamalby.
    bool saveChecked(const char* key, const std::string& value) {
        settings::save();
        const std::string file = settings::path(); // lokalna kopia, jak w settings::load()
        std::ifstream f(file);
        if (!f) return false;
        const std::string want = std::string(key) + "=" + value;
        std::string line;
        while (std::getline(f, line))
            if (line == want) return true;
        return false;
    }

    void editDesktopName() {
        // Dialog zyje w swoim oknie; token strzeze `this` — jesli okno
        // ustawien zamknie sie wczesniej, callback po prostu wraca.
        std::weak_ptr<int> token = lifetimeToken();
        ctx_.prompt(U"Nazwa pulpitu", U"Wpisz nowa nazwe pulpitu:", U"Nazwa: ",
                    text::toUtf32(settings::active.desktopName),
                    [this, token](std::optional<std::u32string> v) {
                        if (!token.lock()) return;
                        if (!v) return;
                        // Pusta nazwa nie dziala: pasek menu i tak wrocilby do
                        // nazwy ze startu (Desktop::drawMenuBar), a po restarcie
                        // zostalaby pusta — wiec odrzucamy z komunikatem.
                        if (v->empty()) { statusRight_ = U"Podaj nazwe"; return; }
                        settings::active.desktopName = text::toUtf8(*v);
                        rebuild();
                        statusRight_ = saveChecked("desktop_name", settings::active.desktopName)
                                           ? U"Zapisano" : U"Nie zapisano";
                    });
    }

    AppContext& ctx_;
    ListView list_;
    StatusBar status_;
    std::u32string statusRight_;
};

class SettingsApp : public App {
public:
    std::string name() const override { return "Ustawienia"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        return std::make_unique<SettingsView>(ctx);
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(52, std::max(36, desktopArea.w - 6));
        int h = std::min(10, std::max(7, desktopArea.h - 4));
        return Rect{desktopArea.x + 4, desktopArea.y + 2, w, h};
    }
};

} // namespace fenrir::apps
