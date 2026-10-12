#pragma once
#include <ctime>
#include <memory>
#include <string>

#include "fenrir/app/App.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Kalendarz — widok miesiaca na siatce 7 kolumn (poniedzialek pierwszy,
// jak w polskich kalendarzach). Strzalki Lewo/Prawo = miesiac, Gora/Dol =
// rok, Home = powrot do dzis. Dzis podswietlone na kolor akcentu.
// Czysta aplikacja "uzytkownika" — jedyny kontakt ze swiatem zewnetrznym
// to zegar (time/localtime, dokladnie jak pasek zadan). Zero zarzadzania
// systemem.
class CalendarView : public Widget {
public:
    CalendarView() { jumpToToday(); }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.windowText, t.windowBg, 0});
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

        // --- Wiersz 2: skróty dni tygodnia (poniedzialek pierwszy) ---
        static const char32_t* kDni[7] = {U"pon", U"wt", U"śr", U"czw", U"pt", U"sob", U"nd"};
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
                const Color fg = isToday ? t.accentText : t.windowText;
                const Color bg = isToday ? t.accentBg : t.windowBg;
                const uint8_t style = isToday ? StyleBold : StyleNone;

                std::u32string s = text::toUtf32(std::to_string(day));
                int cx = x0 + col * kCellW + (kCellW - static_cast<int>(s.size())) / 2;
                if (isToday) {
                    screen.fillRect(Rect{x0 + col * kCellW, y, kCellW, 1},
                                    Cell{U' ', fg, bg, style});
                }
                screen.putText(cx, y, s, fg, bg, style);
                ++day;
            }
            ++y;
        }

        status_.setBounds(Rect{bounds_.x, bounds_.y + bounds_.h - 1, bounds_.w, 1});
        // Tyle co najwyzej 30 znakow — w oknie (min. 30 kolumn) dluzszy tekst
        // byl przycinany i znikaloby "Home".
        status_.setText(U"L/P: miesiac | G/D: rok | Home");
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        switch (ev.key) {
            case Key::Left:    prevMonth(); return KeyEventResult::Consumed;
            case Key::Right:   nextMonth(); return KeyEventResult::Consumed;
            case Key::PageUp:   prevMonth(); return KeyEventResult::Consumed;
            case Key::PageDown: nextMonth(); return KeyEventResult::Consumed;
            case Key::Up:    if (year_ > kMinYear) --year_; return KeyEventResult::Consumed;
            case Key::Down:  if (year_ < kMaxYear) ++year_; return KeyEventResult::Consumed;
            case Key::Home:  jumpToToday(); return KeyEventResult::Consumed;
            default: return KeyEventResult::Ignored;
        }
    }

    bool editable() const override { return false; }

    // Okno nie pozwoli sie zmniejszyc ponizej siatki + odstepow (draw i tak
    // wczesniej odrzuca za wczesne rozmiary — teraz juz tego nie zobaczysz).
    int minWidth() const override { return kGridW + 2; }
    int minHeight() const override { return kMinH; }

private:
    static constexpr int kCellW = 4;  // 7 * 4 = 28 kolumn siatki
    static constexpr int kGridW = 7 * kCellW;
    static constexpr int kMinH = 10;  // tytul + naglowki + odstep + 6 tyg. + status
    static constexpr int kMinYear = 1;    // rok 0 i ujemne = absurd, int by sie przepelnil
    static constexpr int kMaxYear = 9999; // tyle miesie sie w tytule bez zalamywania siatki

    void jumpToToday() {
        const time_t now = time(nullptr);
        if (struct tm* lt = localtime(&now)) {
            year_ = lt->tm_year + 1900;
            month_ = lt->tm_mon + 1;
        }
    }

    void prevMonth() {
        if (month_ > 1) { --month_; return; }
        if (year_ > kMinYear) { month_ = 12; --year_; } // poza zakresem zostajemy
    }
    void nextMonth() {
        if (month_ < 12) { ++month_; return; }
        if (year_ < kMaxYear) { month_ = 1; ++year_; } // poza zakresem zostajemy
    }

    static std::u32string monthName(int m) {
        static const char32_t* kMies[12] = {
            U"Styczeń", U"Luty", U"Marzec", U"Kwiecień", U"Maj", U"Czerwiec",
            U"Lipiec", U"Sierpień", U"Wrzesień", U"Październik", U"Listopad", U"Grudzień"};
        return (m >= 1 && m <= 12) ? kMies[m - 1] : U"?";
    }

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

    // Dzien tygodnia 1-szego dnia miesiaca: 0 = poniedzialek.
    // 1970-01-01 byl czwartkiem (=3 przy poniedzialek=0).
    static int weekdayMondayFirst(int y, int m) {
        const long days = daysFromCivil(y, m, 1);
        return static_cast<int>(((days + 3) % 7 + 7) % 7);
    }

    int year_ = 1970;
    int month_ = 1;
    StatusBar status_;
};

class CalendarApp : public App {
public:
    std::string name() const override { return "Kalendarz"; }

    std::unique_ptr<Widget> createView(AppContext&) override {
        return std::make_unique<CalendarView>();
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(32, std::max(30, desktopArea.w - 6));
        int h = std::min(11, std::max(10, desktopArea.h - 4));
        return Rect{desktopArea.x + 6, desktopArea.y + 2, w, h};
    }
};

} // namespace fenrir::apps
