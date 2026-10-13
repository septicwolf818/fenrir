#pragma once
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/Prefs.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Dialog.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Zdarzenie kalendarza: jednodniowy przedzial godzinowy + tytul.
// Bez wydarzen calodniowych i przechodzacych na nastepny dzien (v1).
struct CalEvent {
    int y = 0, m = 1, d = 1;
    int startMin = 0, endMin = 0;
    std::string title;
};

// Kalendarz — trzy widoki na tym samym kursorze daty (y/m/d + godzina):
//   Miesiac — siatka 7 kolumn (poniedzialek pierwszy), dzis i kursor
//             podswietlone, dni z zdarzeniami oznaczone '*'.
//   Tydzien — 7 kolumn (pon..nd) + lewa kolumna godzin (00:..23:),
//             zdarzenia rysowane jako bloki na planie dnia.
//   Dzien   — to samo, jedna kolumna na pelnej szerokosci.
// Nawigacja: strzalki = kursor daty (L/P: dzien, G/D: tydzien) w widoku
// miesiaca, a w widokach tygodnia/dnia L/P = dzien, G/D = przewijanie
// godzin. PgUp/PgDn = miesiac/tydzien/7 dni, Home = dzis, Esc = powrot do
// miesiaca. Ctrl+Up/Down = rok. Enter: z miesiaca otwiera widok dnia,
// z widokow dnia/tygodnia tworzy zdarzenie. Ctrl+N = nowe zdarzenie,
// Ctrl+T = tydzien, Ctrl+D = dzien (menu: F2).
// Zdarzenia trwale w ~/.config/fenrir/apps/calendar.conf (Prefs).
class CalendarView : public Widget {
public:
    enum class View { Month, Week, Day };

    explicit CalendarView(AppContext& ctx) : ctx_(ctx) {
        jumpToToday();
        loadEvents();
    }

    void setView(View v) { view_ = v; }
    void newEvent();

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.windowText, t.windowBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        switch (view_) {
            case View::Month: drawMonth(screen); break;
            case View::Week:  drawWeek(screen); break;
            case View::Day:   drawDay(screen); break;
        }

        status_.setBounds(Rect{bounds_.x, bounds_.y + bounds_.h - 1, bounds_.w, 1});
        if (!statusMsg_.empty()) {
            status_.setText(text::toUtf32(statusMsg_), {});
        } else if (view_ == View::Month) {
            status_.setText(U"Ent:dzien N:zdarz PgUp/Dn:mies");
        } else if (view_ == View::Week) {
            status_.setText(U"Ent:zdarz PgUp/Dn:tydz Esc:mies");
        } else {
            status_.setText(U"Ent:zdarz PgUp/Dn:7d Esc:mies");
        }
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        statusMsg_.clear();

        // Ctrl+Up/Down = rok (ESC[1;5A / ESC[1;5B) — dziala w kazdym widoku.
        if (ev.ctrl && ev.key == Key::Up)   { shiftYear(-1); return KeyEventResult::Consumed; }
        if (ev.ctrl && ev.key == Key::Down) { shiftYear(1);  return KeyEventResult::Consumed; }

        switch (ev.key) {
            case Key::Home: jumpToToday(); return KeyEventResult::Consumed;
            case Key::Enter:
                if (view_ == View::Month) view_ = View::Day; // do widoku kursora
                else newEvent();
                return KeyEventResult::Consumed;
            case Key::Escape:
                if (view_ != View::Month) { view_ = View::Month; return KeyEventResult::Consumed; }
                return KeyEventResult::Ignored; // z miesiaca Esc = globalne
            default: break;
        }

        if (view_ == View::Month) {
            switch (ev.key) {
                case Key::Left:    moveCursor(-1); return KeyEventResult::Consumed;
                case Key::Right:   moveCursor(1);  return KeyEventResult::Consumed;
                case Key::Up:      moveCursor(-7); return KeyEventResult::Consumed;
                case Key::Down:    moveCursor(7);  return KeyEventResult::Consumed;
                case Key::PageUp:   prevMonth(); return KeyEventResult::Consumed;
                case Key::PageDown: nextMonth(); return KeyEventResult::Consumed;
                default: return KeyEventResult::Ignored;
            }
        }
        // Widoki tygodnia/dnia: L/P = dzien, G/D = godzina, PgUp/Dn = skok tygodnia.
        switch (ev.key) {
            case Key::Left:    moveCursor(-1); return KeyEventResult::Consumed;
            case Key::Right:   moveCursor(1);  return KeyEventResult::Consumed;
            case Key::Up:      hourCur_ = std::max(0, hourCur_ - 1); return KeyEventResult::Consumed;
            case Key::Down:    hourCur_ = std::min(23, hourCur_ + 1); return KeyEventResult::Consumed;
            case Key::PageUp:   moveCursor(-7); return KeyEventResult::Consumed;
            case Key::PageDown: moveCursor(7);  return KeyEventResult::Consumed;
            default: return KeyEventResult::Ignored;
        }
    }

    bool editable() const override { return false; }

    int minWidth() const override { return kGridW + 2; }
    int minHeight() const override { return kMinH; }

private:
    static constexpr int kCellW = 4;  // 7 * 4 = 28 kolumn siatki
    static constexpr int kGridW = 7 * kCellW;
    static constexpr int kMinH = 10;  // tytul + naglowki + odstep + 6 tyg. + status
    static constexpr int kMinYear = 1;
    static constexpr int kMaxYear = 9999;
    static constexpr int kGutterW = 4; // kolumna godzin w widokach tygodnia/dnia

    // --- Zdarzenia: trwale w Prefs (calendar.conf) ---------------------------

    static std::string hmStr(int min) {
        min = std::clamp(min, 0, 23 * 60 + 59); // tez wycisza -Wformat-truncation (glibc)
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d:%02d", min / 60, min % 60);
        return buf;
    }

    static bool parseHm(std::string_view s, int& outMin) {
        if (s.size() != 5 || s[2] != ':') return false;
        auto dig = [](char c) { return c >= '0' && c <= '9'; };
        if (!dig(s[0]) || !dig(s[1]) || !dig(s[3]) || !dig(s[4])) return false;
        const int h = (s[0] - '0') * 10 + (s[1] - '0');
        const int m = (s[3] - '0') * 10 + (s[4] - '0');
        if (h > 23 || m > 59) return false;
        outMin = h * 60 + m;
        return true;
    }

    // "YYYY-MM-DD|HH:MM|HH:MM|tytul" — tytul moze zawierac '|' (limit splits).
    static std::string fmtEvent(const CalEvent& e) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d|%s|%s|", e.y, e.m, e.d,
                      hmStr(e.startMin).c_str(), hmStr(e.endMin).c_str());
        return std::string(buf) + e.title;
    }

    static bool parseEvent(std::string_view s, CalEvent& e) {
        auto dig = [](char c) { return c >= '0' && c <= '9'; };
        // 0-9 data, 10 '|', 11-15 start, 16 '|', 17-21 koniec, 22 '|', 23+ tytul.
        if (s.size() < 23) return false;
        if (!(dig(s[0]) && dig(s[1]) && dig(s[2]) && dig(s[3]) && s[4] == '-' &&
              dig(s[5]) && dig(s[6]) && s[7] == '-' && dig(s[8]) && dig(s[9]) &&
              s[10] == '|' && s[16] == '|' && s[22] == '|'))
            return false;
        int st = 0, en = 0;
        if (!parseHm(s.substr(11, 5), st) || !parseHm(s.substr(17, 5), en)) return false;
        e.y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
        e.m = (s[5] - '0') * 10 + (s[6] - '0');
        e.d = (s[8] - '0') * 10 + (s[9] - '0');
        if (e.y < 1 || e.y > kMaxYear || e.m < 1 || e.m > 12) return false;
        if (e.d < 1 || e.d > daysInMonth(e.y, e.m)) return false;
        if (en <= st) return false;
        e.startMin = st;
        e.endMin = en;
        e.title = std::string(s.substr(23));
        return true;
    }

    void loadEvents() {
        auto& p = prefs::forApp("calendar");
        const int n = p.getInt("next_id", 0);
        events_.clear();
        for (int i = 0; i < n; ++i) {
            CalEvent e;
            if (parseEvent(p.get("e" + std::to_string(i)), e)) events_.push_back(e);
        }
    }

    void addEvent(const CalEvent& e) {
        auto& p = prefs::forApp("calendar");
        const int id = p.getInt("next_id", 0);
        p.set("e" + std::to_string(id), fmtEvent(e));
        p.setInt("next_id", id + 1);
        events_.push_back(e);
    }

    // --- Zapytanie o zdarzenie (Ctrl+N / Enter z widoku dnia/tygodnia) ------

    void clearDrafts() { draftTitle_.clear(); draftStart_.clear(); draftEnd_.clear(); }

    // --- Rysowanie: widok miesiaca ------------------------------------------

    void drawMonth(Screen& screen) {
        const auto& t = theme::active;
        if (bounds_.w < kGridW + 2 || bounds_.h < kMinH) return;

        // "Dzis" liczone na biezaco w kazdej klatce — przeskoczymy polnoc
        // bez odswiezania okna (ten sam trik co zegar na pasku zadan).
        int todayD = 1, todayM = 1, todayY = 1970;
        const time_t now = time(nullptr);
        if (struct tm* lt = localtime(&now)) {
            todayD = lt->tm_mday;
            todayM = lt->tm_mon + 1;
            todayY = lt->tm_year + 1900;
        }

        const int x0 = bounds_.x + (bounds_.w - kGridW) / 2;
        int y = bounds_.y;

        // --- Wiersz 1: nazwa miesiaca i roku, wyrodkowana, pogrubiona ---
        std::u32string title = monthName(month_) + U" " + text::toUtf32(std::to_string(year_));
        int tx = x0 + (kGridW - static_cast<int>(title.size())) / 2;
        screen.putText(tx, y, title, t.headerText, t.windowBg, StyleBold);
        ++y;

        // --- Wiersz 2: skroty dni tygodnia (poniedzialek pierwszy) ---
        for (int c = 0; c < 7; ++c) {
            std::u32string lab = kDni[c];
            int cx = x0 + c * kCellW + (kCellW - static_cast<int>(lab.size())) / 2;
            screen.putText(cx, y, lab, t.headerText, t.windowBg, StyleBold);
        }
        y += 2; // pusty wiersz odstepu po naglowkach

        // --- Siatka dni: maks 6 tygodni ---
        const int firstWd = weekdayMondayFirst(year_, month_); // 0 = poniedzialek
        const int dim = daysInMonth(year_, month_);
        int day = 1;
        for (int row = 0; row < 6 && day <= dim; ++row) {
            for (int col = 0; col < 7; ++col) {
                if (row == 0 && col < firstWd) continue; // pole przed 1-szym
                if (day > dim) break;
                const bool isToday = (day == todayD && month_ == todayM && year_ == todayY);
                const bool isCur = (day == curD_); // kursor jest zawsze w wyswietlanym miesiacu
                Color fg = isToday ? t.accentText : (isCur ? t.titleText : t.windowText);
                Color bg = isToday ? t.accentBg : (isCur ? t.titleBg : t.windowBg);
                uint8_t style = (isToday || isCur) ? StyleBold : StyleNone;

                // Kurs dzis + kursor = nawiasy, zeby oba byli widoczne naraz.
                std::u32string s;
                if (isToday && isCur) s = U"[" + text::toUtf32(std::to_string(day)) + U"]";
                else s = text::toUtf32(std::to_string(day));
                const int cellX = x0 + col * kCellW;
                if (isToday || isCur)
                    screen.fillRect(Rect{cellX, y, kCellW, 1}, Cell{U' ', fg, bg, style});
                int cx = cellX + (kCellW - static_cast<int>(s.size())) / 2;
                screen.putText(cx, y, s, fg, bg, style);

                // Dni z zadarzeniami: '*' na ostatniej kolumnie komorki
                // (nie gryzie sie z dwuznakowym numerem; przy nawiasach pomijamy).
                if (hasEvents(year_, month_, day) && !(isToday && isCur)) {
                    screen.putText(cellX + kCellW - 1, y, U"*", t.highlightText, bg);
                }
                ++day;
            }
            ++y;
        }
    }

    // --- Rysowanie: widok tygodnia ------------------------------------------

    void drawWeek(Screen& screen) {
        const auto& t = theme::active;
        if (bounds_.h < 5 || bounds_.w < kGutterW + 7) return;

        int mY = 0, mM = 0, mD = 0;
        cursorWeek(mY, mM, mD);                       // poniedzialek tygodnia kursora
        int eY = 0, eM = 0, eD = 0;
        civilFromDays(daysFromCivil(mY, mM, mD) + 6, eY, eM, eD); // niedziela

        int y0 = bounds_.y;
        std::u32string title = U"tydzien " + ddmm(mD, mM) + U" - " + ddmm(eD, eM) +
                               U"." + text::toUtf32(std::to_string(eY));
        int tx = bounds_.x + (bounds_.w - static_cast<int>(title.size())) / 2;
        if (tx < bounds_.x) tx = bounds_.x;
        screen.putText(tx, y0, title, t.headerText, t.windowBg, StyleBold);

        // --- Naglowki kolumn: pon..nd z datami; kolumna kursora na akcencie ---
        const int curWd = weekdayOf(year_, month_, curD_);
        const int colW = std::max(1, (bounds_.w - kGutterW) / 7);
        int hy = y0 + 1;
        for (int c = 0; c < 7; ++c) {
            civilFromDays(daysFromCivil(mY, mM, mD) + c, eY, eM, eD);
            std::u32string lab = std::u32string(kDni[c]) + U" " + text::toUtf32(std::to_string(eD));
            lab = text::clip(lab, colW);
            const int cx = bounds_.x + kGutterW + c * colW;
            if (c == curWd) {
                screen.fillRect(Rect{cx, hy, colW, 1}, Cell{U' ', t.accentText, t.accentBg, StyleBold});
                screen.putText(cx, hy, lab, t.accentText, t.accentBg, StyleBold);
            } else {
                screen.putText(cx, hy, lab, t.headerText, t.windowBg, StyleBold);
            }
        }

        const int rowY = y0 + 2;
        const int avail = bounds_.h - 3; // tytul + naglowki + status
        syncScroll(avail);
        const int rows = std::max(0, std::min(avail, 24 - hourScroll_));
        for (int i = 0; i < rows; ++i) {
            const int hour = hourScroll_ + i;
            drawGutter(screen, rowY + i, hour);
            for (int c = 0; c < 7; ++c) {
                civilFromDays(daysFromCivil(mY, mM, mD) + c, eY, eM, eD);
                drawEventCell(screen, bounds_.x + kGutterW + c * colW, colW,
                              rowY + i, eY, eM, eD, hour);
            }
        }
    }

    // --- Rysowanie: widok dnia ------------------------------------------------

    void drawDay(Screen& screen) {
        const auto& t = theme::active;
        if (bounds_.h < 4 || bounds_.w < kGutterW + 3) return;

        const int wd = weekdayOf(year_, month_, curD_);
        int y0 = bounds_.y;
        std::u32string title = std::u32string(kDni[wd]) + U" " + ddmm(curD_, month_) +
                               U"." + text::toUtf32(std::to_string(year_));
        int tx = bounds_.x + (bounds_.w - static_cast<int>(title.size())) / 2;
        if (tx < bounds_.x) tx = bounds_.x;
        screen.putText(tx, y0, title, t.headerText, t.windowBg, StyleBold);

        const int rowY = y0 + 1;
        const int avail = bounds_.h - 2; // tytul + status
        syncScroll(avail);
        const int colX = bounds_.x + kGutterW;
        const int colW = std::max(1, bounds_.w - kGutterW);
        const int rows = std::max(0, std::min(avail, 24 - hourScroll_));
        for (int i = 0; i < rows; ++i) {
            const int hour = hourScroll_ + i;
            drawGutter(screen, rowY + i, hour);
            drawEventCell(screen, colX, colW, rowY + i, year_, month_, curD_, hour);
        }
    }

    // Kolumna godzin: "HH:" + marker '>' na kursorze godziny.
    void drawGutter(Screen& screen, int y, int hour) {
        const auto& t = theme::active;
        const bool cur = (hour == hourCur_);
        const Color bg = cur ? t.accentBg : t.windowBg;
        const Color fg = cur ? t.accentText : t.headerText;
        screen.fillRect(Rect{bounds_.x, y, kGutterW, 1}, Cell{U' ', fg, bg, 0});
        const std::u32string lab = text::toUtf32(hmStr(hour * 60)).substr(0, 3); // "HH:"
        screen.putText(bounds_.x, y, lab, fg, bg, cur ? StyleBold : StyleNone);
        screen.putText(bounds_.x + 3, y, cur ? U">" : U" ", fg, bg, cur ? StyleBold : StyleNone);
    }

    // Komorka zdarzenia: tytul na wierszu startu, '|' na kontynuacji.
    void drawEventCell(Screen& screen, int x, int w, int y, int ey, int em, int ed, int hour) {
        if (w <= 0) return;
        const auto& t = theme::active;
        const CalEvent* e = eventAt(ey, em, ed, hour);
        if (!e) return;
        screen.fillRect(Rect{x, y, w, 1}, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (hour == e->startMin / 60) {
            std::u32string txt = text::clip(text::toUtf32(e->title), w);
            screen.putText(x, y, txt, t.surfaceText, t.surfaceBg, StyleBold);
        } else {
            screen.putText(x, y, U"|", t.dimText, t.surfaceBg);
        }
    }

    // --- Stan / nawigacja -----------------------------------------------------

    const CalEvent* eventAt(int y, int m, int d, int hour) const {
        const int from = hour * 60, to = from + 60;
        for (const auto& e : events_)
            if (e.y == y && e.m == m && e.d == d && e.startMin < to && e.endMin > from)
                return &e;
        return nullptr;
    }

    bool hasEvents(int y, int m, int d) const {
        for (const auto& e : events_)
            if (e.y == y && e.m == m && e.d == d) return true;
        return false;
    }

    // Przesuwa godziny widoczne tak, zeby kursor godziny byl widoczny.
    void syncScroll(int avail) {
        if (avail < 1) { hourScroll_ = 0; return; }
        if (hourCur_ < hourScroll_) hourScroll_ = hourCur_;
        if (hourCur_ >= hourScroll_ + avail) hourScroll_ = hourCur_ - avail + 1;
        hourScroll_ = std::clamp(hourScroll_, 0, std::max(0, 24 - avail));
    }

    void jumpToToday() {
        const time_t now = time(nullptr);
        if (struct tm* lt = localtime(&now)) {
            year_ = lt->tm_year + 1900;
            month_ = lt->tm_mon + 1;
            curD_ = lt->tm_mday;
            hourCur_ = lt->tm_hour;
        }
    }

    void shiftYear(int dy) {
        year_ = std::clamp(year_ + dy, kMinYear, kMaxYear);
        curD_ = std::min(curD_, daysInMonth(year_, month_));
    }

    void prevMonth() {
        if (month_ > 1) { --month_; return; }
        if (year_ > kMinYear) { month_ = 12; --year_; }
        curD_ = std::min(curD_, daysInMonth(year_, month_));
    }
    void nextMonth() {
        if (month_ < 12) { ++month_; return; }
        if (year_ < kMaxYear) { month_ = 1; ++year_; }
        curD_ = std::min(curD_, daysInMonth(year_, month_));
    }

    // Kursor daty przesuniety o delta dni (przechodzi miedzy miesiacami).
    void moveCursor(int deltaDays) {
        long days = daysFromCivil(year_, month_, curD_) + deltaDays;
        days = std::clamp(days, daysFromCivil(kMinYear, 1, 1), daysFromCivil(kMaxYear, 12, 31));
        civilFromDays(days, year_, month_, curD_);
    }

    // Poniedzialek tygodnia kursora.
    void cursorWeek(int& y, int& m, int& d) const {
        civilFromDays(daysFromCivil(year_, month_, curD_) - weekdayOf(year_, month_, curD_), y, m, d);
    }

    static std::u32string ddmm(int d, int m) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02d.%02d", d, m);
        return text::toUtf32(std::string(buf));
    }

    static std::u32string monthName(int m) {
        static const char32_t* kMies[12] = {
            U"Styczeń", U"Luty", U"Marzec", U"Kwiecień", U"Maj", U"Czerwiec",
            U"Lipiec", U"Sierpień", U"Wrzesień", U"Październik", U"Listopad", U"Grudzień"};
        return (m >= 1 && m <= 12) ? kMies[m - 1] : U"?";
    }

    static const char32_t* kDni[7]; // defined below (pon..nd)

    static bool isLeap(int y) {
        return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    }
    static int daysInMonth(int y, int m) {
        static const int kDim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m < 1 || m > 12) return 0;
        return (m == 2 && isLeap(y)) ? 29 : kDim[m - 1];
    }

    // Dni od epoki (proleptyczny gregorianianski, algorytm Hinnanta) —
    // wystarczy do dnia tygodnia, bez <chrono> kalendarza (portable na starych
    // toolchainach).
    static long daysFromCivil(int y, int m, int d) {
        y -= m <= 2;
        const long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
        const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097L + static_cast<long>(doe) - 719468L;
    }

    // Odwrotnosc daysFromCivil (Hinnant): liczba dni -> y/m/d.
    static void civilFromDays(long z, int& y, int& m, int& d) {
        z += 719468;
        const long era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const unsigned doy = doe - (365u * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5u * doy + 2u) / 153u;
        d = static_cast<int>(doy - (153u * mp + 2u) / 5u + 1u);
        m = static_cast<int>(mp) + (mp < 10u ? 3 : -9);
        y = static_cast<int>(yoe) + static_cast<int>(era) * 400 + (m <= 2 ? 1 : 0);
    }

    // Dzien tygodnia dowolnej daty: 0 = poniedzialek (1970-01-01 = czwartek = 3).
    static int weekdayOf(int y, int m, int d) {
        const long days = daysFromCivil(y, m, d);
        return static_cast<int>(((days + 3) % 7 + 7) % 7);
    }

    // Dzien tygodnia 1-szego dnia miesiaca: 0 = poniedzialek.
    static int weekdayMondayFirst(int y, int m) {
        return weekdayOf(y, m, 1);
    }

    AppContext& ctx_;
    View view_ = View::Month;
    int year_ = 1970;
    int month_ = 1;
    int curD_ = 1;        // kursor daty (dzien)
    int hourCur_ = 0;     // kursor godziny (widoki dzien/tydzien)
    int hourScroll_ = 0;  // pierwszy widoczny wiersz godziny
    std::vector<CalEvent> events_;
    std::string draftTitle_, draftStart_, draftEnd_; // zachowane po bledzie walidacji
    std::string statusMsg_;                          // przelotowy komunikat (np. "Dodano: ...")
    StatusBar status_;
};

inline const char32_t* CalendarView::kDni[7] = {U"pon", U"wt", U"śr", U"czw", U"pt", U"sob", U"nd"};

inline void CalendarView::newEvent() {
    const auto token = lifetimeToken();
    if (draftStart_.empty()) {
        // Start = kursor godziny (w widokach dnia/tygodnia) lub teraz;
        // koniec = godzine pozniej (maks. 23:59).
        const int sm = hourCur_ * 60;
        draftStart_ = hmStr(sm);
        draftEnd_ = hmStr(std::min(sm + 60, 23 * 60 + 59));
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Data: %02d.%02d.%04d", curD_, month_, year_);
    auto dlg = std::make_unique<Dialog>(text::toUtf32(std::string(buf)),
                                        Dialog::Buttons::OkCancel);
    dlg->addInput(U"Tytul: ", text::toUtf32(draftTitle_));
    dlg->addInput(U"Start: ", text::toUtf32(draftStart_));
    dlg->addInput(U"Koniec: ", text::toUtf32(draftEnd_));
    Dialog* raw = dlg.get();
    ctx_.openDialog(U"Zdarzenie", std::move(dlg),
                    [this, token, raw](int idx) {
        if (!token.lock()) return;
        if (idx != 0) { clearDrafts(); return; } // Anuluj/Esc — czysty start
        const std::string title = text::toUtf8(raw->input(0)->value());
        const std::string sTxt = text::toUtf8(raw->input(1)->value());
        const std::string eTxt = text::toUtf8(raw->input(2)->value());
        int sm = 0, em = 0;
        if (title.empty()) {
            draftTitle_.clear(); draftStart_ = sTxt; draftEnd_ = eTxt;
            ctx_.showMessage(U"Podaj tytul zdarzenia.");
            return;
        }
        if (!parseHm(sTxt, sm) || !parseHm(eTxt, em)) {
            draftTitle_ = title; draftStart_ = sTxt; draftEnd_ = eTxt;
            ctx_.showMessage(U"Godzina w formacie HH:MM (00:00-23:59).");
            return;
        }
        if (em <= sm) {
            draftTitle_ = title; draftStart_ = sTxt; draftEnd_ = eTxt;
            ctx_.showMessage(U"Koniec musi byc pozniej niz start.");
            return;
        }
        clearDrafts();
        addEvent(CalEvent{year_, month_, curD_, sm, em, title});
        statusMsg_ = "Dodano: " + title;
    });
}

class CalendarApp : public App {
public:
    std::string name() const override { return "Kalendarz"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<CalendarView>(ctx);
        CalendarView* raw = view.get();

        view->setMenuTabs({{U"Widok", std::vector<MenuItem>{
            {U"Miesiac", [raw] { raw->setView(CalendarView::View::Month); }},
            {U"Tydzien (Ctrl+T)", [raw] { raw->setView(CalendarView::View::Week); }},
            {U"Dzien (Ctrl+D)", [raw] { raw->setView(CalendarView::View::Day); }},
        }},
        {U"Zdarzenie", std::vector<MenuItem>{
            {U"Nowe (Ctrl+N)", [raw] { raw->newEvent(); }},
        }}});

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newEvent(); });
        view->pushShortcut({true, Key::Char, U't'}, [raw] { raw->setView(CalendarView::View::Week); });
        view->pushShortcut({true, Key::Char, U'd'}, [raw] { raw->setView(CalendarView::View::Day); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        // Szerzej/wyzej niz kiedys — widok tygodnia ma 7 kolumn + godziny;
        // 48 kolumn = 6 na kolumne, zeby zmiescilo sie "pon 12".
        int w = std::min(48, std::max(30, desktopArea.w - 6));
        int h = std::min(14, std::max(11, desktopArea.h - 4));
        return Rect{desktopArea.x + 4, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir::apps
