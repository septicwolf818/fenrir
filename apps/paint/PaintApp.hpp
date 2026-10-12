#pragma once
#include <algorithm>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Malowanka — rysowanie znakami po siatce (jak klasyczne Painty w terminalu).
// Kursor malowania porusza sie strzalkami; kazdy znak maluje sie w kursorze
// zapamientywanym jako pedzel (Spacja powtarza pedzel, Del/gumka kasuje).
// Ctrl+Z cofa (stos operacji), Ctrl+S/O/N zapis/wczytanie/nowy.
//
// KOLORY: kazda komorka ma swoj fg/bg z palety 16 ANSI (0-15) albo kolor
// domyslny z motywu (255 = "jak tlo edytora"). F3 otwiera palete 16x16
// (kolumny = fg, wiersze = bg, komorki rysowane znakiem polprzezroczystym ▒),
// strzalki wybieraja, Enter ustawia pedzel. Zapis: inline SGR tylko przy
// zmianie koloru — rysunek bez kolorow zapisuje sie jako czysty tekst.
//
// Model (rows_) moze byc wiekszy niz widoczny obszar — wtedy scrollujemy sie
// za kursorem (jak TextArea). Model NIE kurczy sie przy zmniejszaniu okna,
// wiec rysunek przezywa resize w obie strony.
class PaintView : public Widget {
public:
    explicit PaintView(AppContext& ctx) : ctx_(ctx) { rows_.assign(1, std::vector<Px>{}); }

    // --- Operacje z menu / skrotow (App montuje callbacki) --------------------

    void newCanvas() {
        guardDirty([this] {
            rows_.assign(1, std::vector<Px>{});
            curX_ = curY_ = scrollX_ = scrollY_ = 0;
            undo_.clear();
            path_.clear();
            dirty_ = false;
            statusMsg_.clear();
            palOpen_ = false;
            brush_ = Brush{}; // pedzel wraca do kolorow domyslnych
        });
    }

    void openFile() {
        guardDirty([this] {
            const std::string startDir = path_.empty() ? fs::homeDir() : fs::parent(path_);
            const auto token = lifetimeToken();
            ctx_.openFile(startDir, [this, token](const std::string& picked) {
                if (!token.lock()) return;
                loadFrom(picked);
            });
        });
    }

    void save() {
        if (path_.empty()) { saveAs(); return; }
        writeTo(path_);
    }

    void saveAs() {
        const std::string startDir = path_.empty() ? fs::homeDir() : fs::parent(path_);
        const auto token = lifetimeToken();
        ctx_.saveFile(startDir, "obrazek.txt", [this, token](const std::string& picked) {
            if (!token.lock()) return;
            path_ = picked;
            writeTo(picked);
        });
    }

    void undoLast() {
        if (undo_.empty()) { statusMsg_ = "Nic do cofniecia."; return; }
        const PaintOp u = undo_.back();
        undo_.pop_back();
        if (u.y < static_cast<int>(rows_.size()) && u.x < static_cast<int>(rows_[u.y].size())) {
            rows_[u.y][u.x] = u.px; // przywraca znak OBA kolory
            dirty_ = true;
        }
        curY_ = u.y;
        curX_ = u.x;
        statusMsg_.clear();
    }

    // Otwiera palete kolorow (menu Edycja). Stan F3 tez przez to przechodzi.
    void showPalette() {
        if (palOpen_) return;
        palFg_ = brush_.fg == kDefColor ? 7 : brush_.fg;
        palBg_ = brush_.bg == kDefColor ? 0 : brush_.bg;
        palOpen_ = true;
    }

    // --- Widget --------------------------------------------------------------

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        if (bounds_.w <= 0 || bounds_.h <= 0) return;
        const int viewW = bounds_.w;
        const int viewH = std::max(1, bounds_.h - 1); // ostatni wiersz = status

        ensureVisible(viewW, viewH);

        // Tlo powierzchni rysowania.
        screen.fillRect(Rect{bounds_.x, bounds_.y, viewW, viewH},
                        Cell{U' ', t.editorText, t.editorBg, 0});

        for (int r = 0; r < viewH; ++r) {
            const int row = scrollY_ + r;
            if (row >= static_cast<int>(rows_.size())) break; // model jest spojny — dalej puste
            const std::vector<Px>& line = rows_[row];
            const int end = std::min(scrollX_ + viewW, static_cast<int>(line.size()));
            for (int c = scrollX_; c < end; ++c) {
                const Px& p = line[c];
                screen.set(bounds_.x + (c - scrollX_), bounds_.y + r,
                           Cell{p.ch, resolve(p.fg, t.editorText), resolve(p.bg, t.editorBg), 0});
            }
        }

        // Wlasny kursor malowania (odwrocone WYBRANE kolory) — widoczny w kazdym
        // trybie, nie tylko Edit (GUI cursor Desktop stoi w innym miejscu).
        if (!palOpen_ && curY_ >= scrollY_ && curY_ < scrollY_ + viewH &&
            curX_ >= scrollX_ && curX_ < scrollX_ + viewW) {
            const Px p = cellAt(curY_, curX_);
            screen.set(bounds_.x + (curX_ - scrollX_), bounds_.y + (curY_ - scrollY_),
                       Cell{p.ch, resolve(p.bg, t.editorBg), resolve(p.fg, t.editorText), StyleBold});
        }

        // Paleta NAD cala trescia — zaznaczenie sledzi migajacy kursor GUI
        // (Desktop::positionGuiCursor odczytuje cursorPosition()).
        if (palOpen_) drawPalette(screen, viewH);

        status_.setBounds(Rect{bounds_.x, bounds_.y + viewH, bounds_.w, 1});
        std::u32string left;
        if (!statusMsg_.empty()) {
            left = text::toUtf32(statusMsg_);
        } else if (palOpen_) {
            left = U"Paleta: strzalki | Enter=ok | Esc=zamknij";
        } else {
            left = U"Strzalki=ruch | Znak=maluj | Del=gumka | F3=kolory";
        }
        std::u32string right;
        if (palOpen_) {
            right = text::toUtf32("fg:" + std::to_string(palFg_) + " bg:" + std::to_string(palBg_));
        } else {
            right = text::toUtf32(std::to_string(curX_ + 1) + ":" + std::to_string(curY_ + 1));
        }
        // StatusBar rysuje prawa etykiete przy samej krawedzi — bez tego podpowiedz
        // i pozycja kursora nachodza na siebie (widac wtedy zlepek liter).
        left = text::clip(left, std::max(0, bounds_.w - static_cast<int>(right.size()) - 1));
        status_.setText(std::move(left), std::move(right));
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // F3 przełącza palete; gdy otwarta — wszystko idzie do palety
        // (tez ctrl+znaki, zeby nie malowac pod spodem).
        if (ev.key == Key::F3) {
            if (palOpen_) {
                palOpen_ = false;
                return KeyEventResult::Consumed;
            }
            showPalette();
            return KeyEventResult::Consumed;
        }
        if (palOpen_) return handlePaletteKey(ev);

        // Skróty (Ctrl+S/O/N/Z) Desktop obsluguje PRZED widgetem — tutaj
        // tylko przechwytujemy ctrl+znak, zeby nie trafil do pedzla.
        if (ev.key == Key::Char && ev.ctrl) return KeyEventResult::Consumed;

        const int viewH = std::max(1, bounds_.h - 1);
        const int viewW = std::max(1, bounds_.w);
        const int maxY = std::max({1, static_cast<int>(rows_.size()), viewH}) - 1;

        switch (ev.key) {
            case Key::Up:    curY_ = std::max(0, curY_ - 1); return KeyEventResult::Consumed;
            case Key::Down:  curY_ = std::min(curY_ + 1, maxY); return KeyEventResult::Consumed;
            case Key::Left:  curX_ = std::max(0, curX_ - 1); return KeyEventResult::Consumed;
            case Key::Right: {
                // Swobodnie w prawo — tak jak w dol niezalezne od rysowanych
                // znakow: az do konca widoku albo do konca wiersza (pozycja
                // "za znakiem" pozwala dopisywac dalej). Bez cofania: gdy
                // kursor wszedl tu z dluzszego wiersza (Down), nie wraca.
                const int maxX = std::max(rowLength(curY_), viewW - 1);
                if (curX_ < maxX) ++curX_;
                return KeyEventResult::Consumed;
            }
            case Key::PageUp:   curY_ = std::max(0, curY_ - viewH); return KeyEventResult::Consumed;
            case Key::PageDown: curY_ = std::min(curY_ + viewH, maxY); return KeyEventResult::Consumed;
            case Key::Home:  curX_ = 0; return KeyEventResult::Consumed;
            case Key::End:   curX_ = rowLength(curY_); return KeyEventResult::Consumed;
            case Key::Backspace:
            case Key::Delete:
                paint(curY_, curX_, Px{}); // gumka = komorka domyslna
                return KeyEventResult::Consumed;
            case Key::Char: {
                if (ev.character < 32) return KeyEventResult::Consumed; // nie malujemy sterowania
                if (ev.character == U' ') {
                    paint(curY_, curX_, Px{brush_.ch, brush_.fg, brush_.bg}); // Spacja = powtorz pedzel
                } else {
                    paint(curY_, curX_, Px{ev.character, brush_.fg, brush_.bg});
                    brush_.ch = ev.character;
                }
                return KeyEventResult::Consumed;
            }
            default:
                return KeyEventResult::Ignored; // Esc/Tab: fallbacki Desktop
        }
    }

    bool editable() const override { return true; } // znaki od razu maluja (tryb Edit)

    // Niezapisane zmiany rysunku — Desktop pyta o potwierdzenie, zanim zamknie
    // okno z brudna trescia. Czyszcza: Nowy (guardDirty), udane wczytanie
    // (loadFrom) i udany zapis (writeTo).
    bool dirty() const override { return dirty_; }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        const int viewH = std::max(1, bounds_.h - 1);
        if (palOpen_) {
            // Migajacy kursor GUI zaznacza wybrana komorke palety.
            const auto [px, py] = palOrigin(viewH);
            const int x = px + palFg_;
            const int y = py + palBg_;
            if (x >= bounds_.x && x < bounds_.x + bounds_.w &&
                y >= bounds_.y && y < bounds_.y + viewH)
                return std::pair<int, int>{x, y};
            return std::nullopt; // defensywnie (paleta wycieta) — status pokazuje fg/bg
        }
        if (curY_ < scrollY_ || curY_ >= scrollY_ + viewH ||
            curX_ < scrollX_ || curX_ >= scrollX_ + bounds_.w) return std::nullopt;
        return std::pair<int, int>{bounds_.x + curX_ - scrollX_, bounds_.y + curY_ - scrollY_};
    }

    // Minimum: czytelna powierzchnia rysowania + pasek statusu.
    int minWidth() const override { return 20; }
    int minHeight() const override { return 6; }

private:
    // Kolor domyslny = kolor z motywu (255 > 15, wiec nigdy nie wchodzi do SGR).
    static constexpr uint8_t kDefColor = 255;
    static constexpr int kPalSize = 16;  // paleta 16x16 kombinacji fg x bg
    static constexpr int kPalLast = 15;

    struct Px {
        char32_t ch = U' ';
        uint8_t fg = kDefColor;
        uint8_t bg = kDefColor;
        bool operator==(const Px& o) const { return ch == o.ch && fg == o.fg && bg == o.bg; }
        // Komorka czysto "pusta" — do obcinania koncow wierszy przy zapisie.
        bool isDefault() const { return ch == U' ' && fg == kDefColor && bg == kDefColor; }
    };

    struct Brush {
        char32_t ch = U'#';
        uint8_t fg = kDefColor;
        uint8_t bg = kDefColor;
    };

    struct PaintOp { int y; int x; Px px; };

    static Color resolve(uint8_t c, Color def) {
        return c == kDefColor ? def : Color{c};
    }

    // Komorka w modelu; puste pola = domyslna (spacja, kolory motywu).
    Px cellAt(int y, int x) const {
        if (y < 0 || y >= static_cast<int>(rows_.size())) return Px{};
        const std::vector<Px>& line = rows_[y];
        return x < static_cast<int>(line.size()) ? line[x] : Px{};
    }

    // Dlugosc wiersza = najdalsza pozycja, na jaka moze wejsc kursor (append).
    int rowLength(int y) const {
        if (y < 0 || y >= static_cast<int>(rows_.size())) return 0;
        return static_cast<int>(rows_[y].size());
    }

    void paint(int y, int x, const Px& val) {
        if (y < 0 || x < 0) return;
        const Px old = cellAt(y, x); // brak pola = komorka domyslna
        if (old == val) return; // nic sie nie zmienia — nie rozciagamy wiersza
        if (y >= static_cast<int>(rows_.size())) rows_.resize(static_cast<std::size_t>(y) + 1);
        std::vector<Px>& line = rows_[y];
        if (x >= static_cast<int>(line.size())) line.resize(static_cast<std::size_t>(x) + 1);
        undo_.push_back({y, x, old});
        if (undo_.size() > 1000) undo_.erase(undo_.begin());
        line[x] = val;
        dirty_ = true;
        statusMsg_.clear();
    }

    void ensureVisible(int viewW, int viewH) {
        if (curY_ < scrollY_) scrollY_ = curY_;
        if (viewH > 0 && curY_ >= scrollY_ + viewH) scrollY_ = curY_ - viewH + 1;
        if (curX_ < scrollX_) scrollX_ = curX_;
        if (viewW > 0 && curX_ >= scrollX_ + viewW) scrollX_ = curX_ - viewW + 1;
        scrollY_ = std::max(0, scrollY_);
        scrollX_ = std::max(0, scrollX_);
    }

    // --- Paleta 16x16 ----------------------------------------------------------

    // Punkt poczatkowy palety (wycentrowana w obszarze rysowania).
    // Gdy okno jest za niskie na 16 wierszy — przesuwamy ja w gore, zeby
    // wybrany wiersz bg zawsze byl widoczny (jak scroll za kursorem).
    std::pair<int, int> palOrigin(int viewH) const {
        const int px = bounds_.x + std::max(0, (bounds_.w - kPalSize) / 2);
        int py = bounds_.y + std::max(0, (viewH - kPalSize) / 2);
        if (viewH < kPalSize) py = bounds_.y - std::min(palBg_, kPalSize - viewH);
        return {px, py};
    }

    void drawPalette(Screen& screen, int viewH) const {
        const auto& t = theme::active;
        const auto [px, py] = palOrigin(viewH);
        const int w = std::min(kPalSize, bounds_.x + bounds_.w - px);
        const int y0 = std::max(py, bounds_.y); // wycinek palety widoczny w oknie
        const int y1 = std::min(py + kPalSize, bounds_.y + viewH);
        if (w <= 0 || y1 <= y0) return;
        screen.fillRect(Rect{px, y0, w, y1 - y0}, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        for (int bg = 0; bg < kPalSize; ++bg) {
            for (int fg = 0; fg < kPalSize; ++fg) {
                const int x = px + fg;
                const int y = py + bg;
                if (x < bounds_.x || x >= bounds_.x + bounds_.w) continue;
                if (y < bounds_.y || y >= bounds_.y + viewH) continue;
                // Komorki polprzezroczystym blokiem — widac oba skladniki pary.
                screen.set(x, y, Cell{U'▒',
                                      Color{static_cast<uint8_t>(fg)},
                                      Color{static_cast<uint8_t>(bg)}, 0});
            }
        }
    }

    KeyEventResult handlePaletteKey(const KeyEvent& ev) {
        switch (ev.key) {
            case Key::Up:      palBg_ = std::max(0, palBg_ - 1); return KeyEventResult::Consumed;
            case Key::Down:    palBg_ = std::min(kPalLast, palBg_ + 1); return KeyEventResult::Consumed;
            case Key::Left:    palFg_ = std::max(0, palFg_ - 1); return KeyEventResult::Consumed;
            case Key::Right:   palFg_ = std::min(kPalLast, palFg_ + 1); return KeyEventResult::Consumed;
            case Key::Home:    palFg_ = 0; return KeyEventResult::Consumed;
            case Key::End:     palFg_ = kPalLast; return KeyEventResult::Consumed;
            case Key::PageUp:  palBg_ = 0; return KeyEventResult::Consumed;
            case Key::PageDown: palBg_ = kPalLast; return KeyEventResult::Consumed;
            case Key::Enter:   // zatwierdz — pedzel dostaje wybrana pare kolorow
                brush_.fg = static_cast<uint8_t>(palFg_);
                brush_.bg = static_cast<uint8_t>(palBg_);
                palOpen_ = false;
                statusMsg_.clear();
                return KeyEventResult::Consumed;
            case Key::Escape:  // anuluj — pedzel bez zmian
                palOpen_ = false;
                return KeyEventResult::Consumed;
            default:
                return KeyEventResult::Consumed; // paleta otwarta — nic pod spodem
        }
    }

    // --- Zapis / wczytywanie (inline SGR, tylko przy zmianie koloru) ------------

    static std::string fgCode(uint8_t c) {
        if (c == kDefColor) return "39";
        return std::to_string(c < 8 ? 30 + c : 90 + (c - 8));
    }
    static std::string bgCode(uint8_t c) {
        if (c == kDefColor) return "49";
        return std::to_string(c < 8 ? 40 + c : 100 + (c - 8));
    }

    // Zapis: koncowe komorki domyslne obcinamy; kolory jako SGR tylko gdy sie
    // zmieniaja — rysunek bez kolorow wychodzi jako czysty tekst (zgodny ze
    // starymi plikami). Kolorowa spacja na koncu to ART — nie jest obcinana.
    void writeTo(const std::string& path) {
        std::ofstream f(path, std::ios::trunc);
        if (!f) { statusMsg_ = "Nie mozna zapisac: " + path; return; }
        for (const auto& line : rows_) {
            std::size_t end = line.size();
            while (end > 0 && line[end - 1].isDefault()) --end;

            std::string out;
            uint8_t cFg = kDefColor;
            uint8_t cBg = kDefColor;
            for (std::size_t i = 0; i < end; ++i) {
                const Px& p = line[i];
                if (p.fg != cFg || p.bg != cBg) {
                    out += "\x1b[";
                    bool first = true;
                    if (p.fg != cFg) { out += fgCode(p.fg); first = false; }
                    if (p.bg != cBg) {
                        if (!first) out += ';';
                        out += bgCode(p.bg);
                        first = false;
                    }
                    out += 'm';
                    cFg = p.fg;
                    cBg = p.bg;
                }
                out += text::toUtf8(std::u32string_view(&p.ch, 1));
            }
            if (cFg != kDefColor || cBg != kDefColor) out += "\x1b[0m";
            f << out << '\n';
        }
        if (!f) { statusMsg_ = "Blad zapisu: " + path; return; }
        dirty_ = false;
        statusMsg_ = "Zapisano: " + fs::nameOf(path);
    }

    // Parsuje jeden wiersz pliku: fragmenty tekstu + sekwencje SGR.
    // Obslugiwane: 0 (reset), 30-37/90-97 (fg), 40-47/100-107 (bg), 39/49.
    // Pozostale kody (pogrubienia itp.) ignorowane; znaki bez SGR = kolor domyslny.
    static std::vector<Px> parseLine(const std::string& raw) {
        std::vector<Px> row;
        uint8_t fg = kDefColor;
        uint8_t bg = kDefColor;
        std::size_t i = 0;
        while (i < raw.size()) {
            const std::size_t esc = raw.find('\x1b', i);
            const std::string chunk =
                raw.substr(i, esc == std::string::npos ? std::string::npos : esc - i);
            if (!chunk.empty()) {
                for (char32_t cp : text::toUtf32(chunk)) row.push_back(Px{cp, fg, bg});
            }
            if (esc == std::string::npos) break;
            i = esc;
            if (i + 1 < raw.size() && raw[i + 1] == '[') {
                std::size_t j = i + 2;
                while (j < raw.size() && raw[j] != 'm') ++j;
                if (j >= raw.size()) break; // niekompletna sekwencja — koniec wiersza
                applySgr(raw.substr(i + 2, j - (i + 2)), fg, bg);
                i = j + 1;
            } else {
                ++i; // sam ESC (stary plik) — pomin
            }
        }
        return row;
    }

    static void applySgr(const std::string& params, uint8_t& fg, uint8_t& bg) {
        std::size_t pos = 0;
        while (pos <= params.size()) {
            const std::size_t sep = params.find(';', pos);
            const std::string tok =
                params.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
            int code = 0;
            bool any = false;
            for (char c : tok) {
                if (c >= '0' && c <= '9') { code = code * 10 + (c - '0'); any = true; }
            }
            if (!any) code = 0; // pusty parametr = domyslny (jak w terminalu)
            if (code == 0) { fg = kDefColor; bg = kDefColor; }
            else if (code >= 30 && code <= 37) fg = static_cast<uint8_t>(code - 30);
            else if (code >= 90 && code <= 97) fg = static_cast<uint8_t>(code - 90 + 8);
            else if (code == 39) fg = kDefColor;
            else if (code >= 40 && code <= 47) bg = static_cast<uint8_t>(code - 40);
            else if (code >= 100 && code <= 107) bg = static_cast<uint8_t>(code - 100 + 8);
            else if (code == 49) bg = kDefColor;
            if (sep == std::string::npos) break;
            pos = sep + 1;
        }
    }

    void loadFrom(const std::string& path) {
        std::ifstream f(path);
        if (!f) { statusMsg_ = "Nie mozna otworzyc: " + path; return; }
        rows_.clear();
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            rows_.push_back(parseLine(line));
        }
        if (rows_.empty()) rows_.assign(1, std::vector<Px>{});
        curX_ = curY_ = scrollX_ = scrollY_ = 0;
        undo_.clear();
        path_ = path;
        dirty_ = false;
        palOpen_ = false;
        statusMsg_ = "Wczytano: " + fs::nameOf(path);
    }

    // Jak w Edytorze: brudny plik -> pytamy przed zgubieniem zmian.
    void guardDirty(std::function<void()> proceed) {
        if (!dirty_) { proceed(); return; }
        const auto token = lifetimeToken();
        ctx_.confirm(U"Niezapisane zmiany. Kontynuowac?",
                     [token, proceed = std::move(proceed)](bool ok) {
                         if (!token.lock()) return;
                         if (ok) proceed();
                     });
    }

    AppContext& ctx_;
    std::vector<std::vector<Px>> rows_; // model rysunku (moze przewyzszac widok)
    std::vector<PaintOp> undo_;         // stos cofania, cap 1000
    int curX_ = 0, curY_ = 0;           // kursor malowania (wspolrzedne modelu)
    int scrollX_ = 0, scrollY_ = 0;     // przesuniecie widoku za kursorem
    Brush brush_;                       // pedzel: znak + para kolorow
    bool palOpen_ = false;              // paleta otwarta?
    int palFg_ = 7, palBg_ = 0;         // wybrana kolumna/wiersz palety (0..15)
    bool dirty_ = false;
    std::string path_;                  // "" = jeszcze nie zapisywany
    std::string statusMsg_;
    StatusBar status_;
};

class PaintApp : public App {
public:
    std::string name() const override { return "Malowanka"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<PaintView>(ctx);
        PaintView* raw = view.get();

        view->setMenuTabs({{U"Plik", std::vector<MenuItem>{
            {U"Nowy (Ctrl+N)", [raw] { raw->newCanvas(); }},
            {U"Otworz... (Ctrl+O)", [raw] { raw->openFile(); }},
            {U"Zapisz (Ctrl+S)", [raw] { raw->save(); }},
            {U"Zapisz jako...", [raw] { raw->saveAs(); }},
        }},
        {U"Edycja", std::vector<MenuItem>{
            {U"Cofnij (Ctrl+Z)", [raw] { raw->undoLast(); }},
            {U"Kolory (F3)", [raw] { raw->showPalette(); }},
        }}});

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newCanvas(); });
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openFile(); });
        view->pushShortcut({true, Key::Char, U's'}, [raw] { raw->save(); });
        view->pushShortcut({true, Key::Char, U'z'}, [raw] { raw->undoLast(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(30, desktopArea.w - 6));
        // Wysokosc 20 = paleta 16 wierszy + pasek statusu + ramka i pasek menu
        // okna (tresc = h - 3 => h = 20, gdy paleta ma sie cala zmiescic).
        // Sciete tez do obszaru pulpitu - zwykle okno, nie "na caly pulpit".
        int h = std::min(20, std::max(8, desktopArea.h - 4));
        return Rect{desktopArea.x + 5, desktopArea.y + 2, w, h};
    }
};

} // namespace fenrir::apps
