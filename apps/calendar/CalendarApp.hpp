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
#include "fenrir/core/Notifications.hpp"
#include "fenrir/core/Prefs.hpp"
#include "fenrir/core/Settings.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Dialog.hpp"
#include "fenrir/widgets/SelectionList.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Calendar event: a time range within one day, plus a title.
// No all-day events or events spanning multiple days (v1).
struct CalEvent {
    int y = 0, m = 1, d = 1;
    int startMin = 0, endMin = 0;
    std::string title;
    int id = -1; // Prefs key ("e<id>"), -1 = not saved yet
};

// Calendar — three views sharing the same date cursor (y/m/d + hour):
//   Month — 7-column grid (Monday first), today and cursor highlighted,
//           event days marked with '*', and the selected day's agenda below
//           the grid: one row per event (time + title, sorted by start).
//           Tab moves keyboard focus into the agenda — arrows select an
//           event, Enter edits it, Delete deletes it, Tab returns to the grid.
//   Week  — 7 columns (Mon..Sun) + a left-hand hour column (00:..23:),
//           with events drawn as blocks in the daily schedule.
//   Day   — the same layout, with one full-width column.
// Navigation: arrows move the date cursor (Left/Right: day, Up/Down: week)
// in the month view; in week/day views Left/Right move by day and Up/Down
// scroll by hour. PgUp/PgDn = month/week/7 days, Home = today, Backspace =
// return to month. Ctrl+Up/Down = year. Enter: from month, open day view;
// from day/week, edit the event under the cursor (or create one if none).
// Delete removes the event under the cursor (after confirmation).
// Multiple events in the same hour: the cell draws all of them (the selected
// one is prefixed with '>'); Tab changes the selection — Enter/Del act on it.
// In the month view Tab moves keyboard focus into the day's agenda list
// (arrows select, Enter edits, Delete deletes, Tab returns to the grid).
// Ctrl+N = new event, Ctrl+T = week, Ctrl+D = day (menu: F2).
// Events persist in ~/.config/fenrir/apps/calendar.conf (Prefs).
class CalendarView : public Widget {
public:
    enum class View { Month, Week, Day };

    explicit CalendarView(AppContext& ctx) : ctx_(ctx) {
        jumpToToday();
        loadEvents();
        // Enter/Space on an agenda row opens the event for editing.
        agenda_.onSelect = [this](int row) {
            if (const CalEvent* e = agendaEventAt(row)) eventDialog(e);
        };
    }

    void setView(View v) { view_ = v; }
    void newEvent();
    void eventDialog(const CalEvent* existing);
    void confirmDelete(const CalEvent& e);

    // Scan calendar events and push notifications (missed on startup, or
    // starting now during background polling).
    static void pollNotifications(AppContext& ctx, bool startup = false);

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
        if (view_ == View::Month) {
            if (agendaFocus_)
                status_.setText(U"Enter: edit Del: delete Tab: back");
            else if (hasEvents(year_, month_, curD_))
                status_.setText(U"Tab: events Enter: day Ctrl+N: event");
            else
                status_.setText(U"Enter: day Ctrl+N: event PgUp/Dn: month");
        } else if (view_ == View::Week) {
            status_.setText(multiAtCursor() ? U"Enter: edit Tab: next Del: delete PgUp/Dn: week"
                                            : U"Enter: edit/add Del: delete PgUp/Dn: week");
        } else {
            status_.setText(multiAtCursor() ? U"Enter: edit Tab: next Del: delete PgUp/Dn: 7d"
                                            : U"Enter: edit/add Del: delete PgUp/Dn: 7d");
        }
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Moving the date/hour cursor resets the slot selection — Tab selects
        // among events in the same hour, so after moving we return to 0.
        resetSlotSelectionIfCursorMoved();

        // Ctrl+Up/Down = year (ESC[1;5A / ESC[1;5B) — works in every view.
        if (ev.ctrl && ev.key == Key::Up)   { shiftYear(-1); return KeyEventResult::Consumed; }
        if (ev.ctrl && ev.key == Key::Down) { shiftYear(1);  return KeyEventResult::Consumed; }

        // Month-view agenda focus (entered with Tab): the SelectionList owns
        // Up/Down/PageUp/PageDown/Home/End (selection + scrolling) and
        // Enter/Space (edit via onSelect); Left/Right still change the day,
        // Delete deletes the selected event, Tab returns to the grid.
        // No Alt-based scrolling here — a bare Linux console may never emit
        // Alt+arrow sequences, so they cannot be relied on.
        if (view_ == View::Month && agendaFocus_) {
            switch (ev.key) {
                case Key::Up:
                case Key::Down:
                case Key::PageUp:
                case Key::PageDown:
                case Key::Home:
                case Key::End:
                case Key::Enter:
                case Key::Char: {
                    // Enter/Space selects via onSelect (= edit); other
                    // characters are not consumed (the agenda has no input).
                    const auto r = agenda_.handleKey(ev);
                    return r == KeyEventResult::Ignored ? KeyEventResult::Ignored
                                                        : KeyEventResult::Consumed;
                }
                case Key::Left:  moveCursor(-1); return KeyEventResult::Consumed;
                case Key::Right: moveCursor(1);  return KeyEventResult::Consumed;
                case Key::Delete: {
                    if (const CalEvent* e = agendaEventAt(agenda_.selectedIndex()))
                        confirmDelete(*e);
                    return KeyEventResult::Consumed;
                }
                case Key::Tab:
                    agendaFocus_ = false;
                    return KeyEventResult::Consumed;
                default: return KeyEventResult::Ignored;
            }
        }

        switch (ev.key) {
            case Key::Home: jumpToToday(); return KeyEventResult::Consumed;
            case Key::Enter: {
                if (view_ == View::Month) { view_ = View::Day; return KeyEventResult::Consumed; }
                // If there's an event under the cursor, edit the selected one
                // (Tab selects the next); otherwise create a new event.
                if (const CalEvent* e = selectedEvent(year_, month_, curD_, hourCur_))
                    eventDialog(e);
                else
                    newEvent();
                return KeyEventResult::Consumed;
            }
            // Backspace returns from day/week to month. Esc is intentionally
            // not handled here — Desktop reserves it for switching cursor
            // modes (Edit->Keyboard->Desktop); apps may use Esc only in
            // popups (Dialog).
            case Key::Backspace:
                if (view_ != View::Month) { view_ = View::Month; return KeyEventResult::Consumed; }
                return KeyEventResult::Ignored; // In month view, Backspace is global.
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
                case Key::Delete:  return KeyEventResult::Consumed; // no hour context
                case Key::Tab: // focus the agenda list (when the day has events)
                    if (hasEvents(year_, month_, curD_)) {
                        agendaFocus_ = true;
                        agenda_.setSelectedIndex(0);
                        return KeyEventResult::Consumed;
                    }
                    return KeyEventResult::Ignored;
                default: return KeyEventResult::Ignored;
            }
        }
        // Week/day views: Left/Right = day, Up/Down = hour, PgUp/Dn = jump a week.
        switch (ev.key) {
            case Key::Left:    moveCursor(-1); return KeyEventResult::Consumed;
            case Key::Right:   moveCursor(1);  return KeyEventResult::Consumed;
            case Key::Up:
                hourCur_ = std::max(0, hourCur_ - 1);
                resetSlotSelectionIfCursorMoved();
                return KeyEventResult::Consumed;
            case Key::Down:
                hourCur_ = std::min(23, hourCur_ + 1);
                resetSlotSelectionIfCursorMoved();
                return KeyEventResult::Consumed;
            case Key::PageUp:   moveCursor(-7); return KeyEventResult::Consumed;
            case Key::PageDown: moveCursor(7);  return KeyEventResult::Consumed;
            case Key::Tab: {
                // Select among events in the same hour (Enter/Del use this selection).
                auto list = slotEvents(year_, month_, curD_, hourCur_);
                if (list.size() < 2) return KeyEventResult::Ignored;
                if (slotSel_ >= static_cast<int>(list.size())) slotSel_ = 0;
                slotSel_ = (slotSel_ + 1) % static_cast<int>(list.size());
                return KeyEventResult::Consumed;
            }
            case Key::Delete: {
                if (const CalEvent* e = selectedEvent(year_, month_, curD_, hourCur_)) confirmDelete(*e);
                return KeyEventResult::Consumed;
            }
            default: return KeyEventResult::Ignored;
        }
    }

    bool editable() const override { return false; }

    int minWidth() const override { return kGridW + 2; }
    int minHeight() const override { return kMinH; }

private:
    static constexpr int kCellW = 4;  // 7 * 4 = 28 grid columns
    static constexpr int kGridW = 7 * kCellW;
    static constexpr int kMinH = 10;  // title + headers + gap + 6 weeks + status
    static constexpr int kMinYear = 1;
    static constexpr int kMaxYear = 9999;
    static constexpr int kGutterW = 4; // hour column in week/day views

    // --- Events: persisted in Prefs (calendar.conf) ---------------------------

    static std::string hmStr(int min) {
        min = std::clamp(min, 0, 23 * 60 + 59);
        const int hours = min / 60;
        const int minutes = min % 60;
        return {static_cast<char>('0' + hours / 10), static_cast<char>('0' + hours % 10), ':',
                static_cast<char>('0' + minutes / 10), static_cast<char>('0' + minutes % 10)};
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

    // "YYYY-MM-DD|HH:MM|HH:MM|title" — title may contain '|' (limited splits).
    static std::string fmtEvent(const CalEvent& e) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d|%s|%s|", e.y, e.m, e.d,
                      hmStr(e.startMin).c_str(), hmStr(e.endMin).c_str());
        return std::string(buf) + e.title;
    }

    static bool parseEvent(std::string_view s, CalEvent& e) {
        auto dig = [](char c) { return c >= '0' && c <= '9'; };
        // 0-9 date, 10 '|', 11-15 start, 16 '|', 17-21 end, 22 '|', 23+ title.
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
            if (parseEvent(p.get("e" + std::to_string(i)), e)) {
                e.id = i;
                events_.push_back(e);
            }
        }
    }

    void addEvent(CalEvent e) {
        auto& p = prefs::forApp("calendar");
        const int id = p.getInt("next_id", 0);
        p.set("e" + std::to_string(id), fmtEvent(e));
        p.setInt("next_id", id + 1);
        e.id = id;
        events_.push_back(std::move(e));
    }

    void updateEvent(const CalEvent& e) {
        if (e.id < 0) return;
        prefs::forApp("calendar").set("e" + std::to_string(e.id), fmtEvent(e));
        for (auto& v : events_)
            if (v.id == e.id) { v = e; break; }
    }

    void deleteEvent(int id) {
        prefs::forApp("calendar").set("e" + std::to_string(id), ""); // set("") deletes the key
        std::erase_if(events_, [id](const CalEvent& v) { return v.id == id; });
    }

    // --- Event form (Ctrl+N / Enter from day/week view) ----------------------

    void clearDrafts() {
        draftTitle_.clear();
        draftStart_.clear();
        draftEnd_.clear();
        draftEditId_ = -1; // -1 = create mode, >=0 = edit mode for that id
    }

    // --- Drawing: month view --------------------------------------------------

    void drawMonth(Screen& screen) {
        // Agenda rows for the cursor day (rebuilt every frame — cheap for a
        // handful of events). Focus is dropped when there is nothing to select.
        agenda_.setItems(agendaRows());
        if (agenda_.empty()) agendaFocus_ = false;

        const auto& t = theme::active;
        if (bounds_.w < kGridW + 2 || bounds_.h < kMinH) return;

        // Recalculate "today" on every frame — this handles crossing midnight
        // without refreshing the window (same trick as the taskbar clock).
        int todayD = 1, todayM = 1, todayY = 1970;
        const time_t now = time(nullptr);
        if (struct tm* lt = localtime(&now)) {
            todayD = lt->tm_mday;
            todayM = lt->tm_mon + 1;
            todayY = lt->tm_year + 1900;
        }

        const int x0 = bounds_.x + (bounds_.w - kGridW) / 2;
        int y = bounds_.y;

        // --- Row 1: centered, bold month and year -----------------------------
        std::u32string title = monthName(month_) + U" " + text::toUtf32(std::to_string(year_));
        int tx = x0 + (kGridW - static_cast<int>(title.size())) / 2;
        screen.putText(tx, y, title, t.windowText, t.windowBg, StyleBold);
        ++y;

        // --- Row 2: weekday abbreviations (Monday first) ----------------------
        for (int c = 0; c < 7; ++c) {
            std::u32string lab = kDni[c];
            int cx = x0 + c * kCellW + (kCellW - static_cast<int>(lab.size())) / 2;
            screen.putText(cx, y, lab, t.windowText, t.windowBg, StyleBold);
        }
        y += 2; // blank spacer row after the headers

        // --- Day grid: up to 6 weeks -----------------------------------------
        const int firstWd = weekdayMondayFirst(year_, month_); // 0 = Monday
        const int dim = daysInMonth(year_, month_);
        int day = 1;
        for (int row = 0; row < 6 && day <= dim; ++row) {
            for (int col = 0; col < 7; ++col) {
                if (row == 0 && col < firstWd) continue; // cell before the 1st
                if (day > dim) break;
                const bool isToday = (day == todayD && month_ == todayM && year_ == todayY);
                const bool isCur = (day == curD_); // cursor is always in the displayed month
                Color fg = isToday ? t.accentText : (isCur ? t.titleText : t.windowText);
                Color bg = isToday ? t.accentBg : (isCur ? t.titleBg : t.windowBg);
                uint8_t style = (isToday || isCur) ? StyleBold : StyleNone;

                // Today + cursor = brackets, so both are visible at once.
                std::u32string s;
                if (isToday && isCur) s = U"[" + text::toUtf32(std::to_string(day)) + U"]";
                else s = text::toUtf32(std::to_string(day));
                const int cellX = x0 + col * kCellW;
                if (isToday || isCur)
                    screen.fillRect(Rect{cellX, y, kCellW, 1}, Cell{U' ', fg, bg, style});
                int cx = cellX + (kCellW - static_cast<int>(s.size())) / 2;
                screen.putText(cx, y, s, fg, bg, style);

                // Event days: '*' in the cell's last column (does not conflict
                // with two-digit dates; omitted when brackets are shown).
                if (hasEvents(year_, month_, day) && !(isToday && isCur)) {
                    screen.putText(cellX + kCellW - 1, y, U"*", t.highlightText, bg);
                }
                ++day;
            }
            ++y;
        }

        // Cursor agenda: one row per event ("HH:MM title", sorted by start
        // time) in a SelectionList filling the rows between the grid and the
        // status bar (blank when there are no events). Unfocused the list is
        // dimmed (inactive); Tab focuses it for selection + Enter-to-edit.
        const int statusY = bounds_.y + bounds_.h - 1;
        const int avail = statusY - y;
        if (avail <= 0) {
            agendaFocus_ = false;
        } else {
            agenda_.setBounds(Rect{bounds_.x, y, bounds_.w, avail});
            if (agendaY_ != year_ || agendaM_ != month_ || agendaD_ != curD_) {
                agendaY_ = year_;
                agendaM_ = month_;
                agendaD_ = curD_;
                agenda_.setSelectedIndex(0);
            }
            agenda_.setEnabled(agendaFocus_);
            agenda_.draw(screen);
        }
    }

    // --- Drawing: week view ---------------------------------------------------

    void drawWeek(Screen& screen) {
        agendaFocus_ = false; // the agenda list lives in the month view only
        const auto& t = theme::active;
        if (bounds_.h < 5 || bounds_.w < kGutterW + 7) return;

        int mY = 0, mM = 0, mD = 0;
        cursorWeek(mY, mM, mD);                       // Monday of the cursor's week
        int eY = 0, eM = 0, eD = 0;
        civilFromDays(daysFromCivil(mY, mM, mD) + 6, eY, eM, eD); // Sunday

        int y0 = bounds_.y;
        std::u32string title = U"Week " + ddmm(mD, mM);
        if (mY != eY) title += U"." + text::toUtf32(std::to_string(mY));
        title += U" - " + ddmm(eD, eM) + U"." + text::toUtf32(std::to_string(eY));
        int tx = bounds_.x + (bounds_.w - static_cast<int>(title.size())) / 2;
        if (tx < bounds_.x) tx = bounds_.x;
        screen.putText(tx, y0, title, t.windowText, t.windowBg, StyleBold);

        // --- Column headers: Mon..Sun with dates; cursor column highlighted --
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
                screen.putText(cx, hy, lab, t.windowText, t.windowBg, StyleBold);
            }
        }

        const int rowY = y0 + 2;
        const int avail = bounds_.h - 3; // title + headers + status
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

    // --- Drawing: day view ----------------------------------------------------

    void drawDay(Screen& screen) {
        agendaFocus_ = false; // the agenda list lives in the month view only
        const auto& t = theme::active;
        if (bounds_.h < 4 || bounds_.w < kGutterW + 3) return;

        const int wd = weekdayOf(year_, month_, curD_);
        int y0 = bounds_.y;
        std::u32string title = std::u32string(kDni[wd]) + U" " + ddmm(curD_, month_) +
                               U"." + text::toUtf32(std::to_string(year_));
        int tx = bounds_.x + (bounds_.w - static_cast<int>(title.size())) / 2;
        if (tx < bounds_.x) tx = bounds_.x;
        screen.putText(tx, y0, title, t.windowText, t.windowBg, StyleBold);

        const int rowY = y0 + 1;
        const int avail = bounds_.h - 2; // title + status
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

    // Hour column: "HH:" + '>' marker on the selected hour.
    void drawGutter(Screen& screen, int y, int hour) {
        const auto& t = theme::active;
        const bool cur = (hour == hourCur_);
        const Color bg = cur ? t.accentBg : t.windowBg;
        const Color fg = cur ? t.accentText : t.windowText;
        screen.fillRect(Rect{bounds_.x, y, kGutterW, 1}, Cell{U' ', fg, bg, 0});
        const std::u32string lab = text::toUtf32(hmStr(hour * 60)).substr(0, 3); // "HH:"
        screen.putText(bounds_.x, y, lab, fg, bg, cur ? StyleBold : StyleNone);
        screen.putText(bounds_.x + 3, y, cur ? U">" : U" ", fg, bg, cur ? StyleBold : StyleNone);
    }

    // Event cell: titles of events STARTING in this hour — the selected event
    // is prefixed with '>' (visible even when clipped), followed by the rest.
    // Continuation hours (no event starts) use '|', as before.
    void drawEventCell(Screen& screen, int x, int w, int y, int ey, int em, int ed, int hour) {
        if (w <= 0) return;
        const auto& t = theme::active;
        const auto list = slotEvents(ey, em, ed, hour);
        if (list.empty()) return;
        screen.fillRect(Rect{x, y, w, 1}, Cell{U' ', t.surfaceText, t.surfaceBg, 0});

        bool startsHere = false;
        for (const auto* e : list)
            if (e->startMin / 60 == hour) { startsHere = true; break; }

        // Show '>' only in the cursor cell (where Tab selection applies).
        const bool cursorSlot = (ey == year_ && em == month_ && ed == curD_ && hour == hourCur_);
        const CalEvent* sel = nullptr;
        if (cursorSlot && selY_ == year_ && selM_ == month_ && selD_ == curD_ && selH_ == hourCur_ &&
            slotSel_ >= 0 && slotSel_ < static_cast<int>(list.size()))
            sel = list[static_cast<std::size_t>(slotSel_)];

        if (!startsHere) {
            if (sel) {
                // A selected event can continue through this hour. Keep the
                // continuation bar, but expose both the selection and title.
                screen.putText(x, y, text::clip(U">|" + text::toUtf32(sel->title), w),
                               t.highlightText, t.surfaceBg, StyleBold);
            } else {
                screen.putText(x, y, U"|", t.dimText, t.surfaceBg);
            }
            return;
        }

        if (sel && sel->startMin / 60 != hour) {
            // The selected event is a continuation; show it first so Tab
            // selection remains visible alongside events starting this hour.
            std::u32string txt = U">|" + text::toUtf32(sel->title);
            for (const auto* e : list)
                if (e != sel && e->startMin / 60 == hour)
                    txt += U"; " + text::toUtf32(e->title);
            screen.putText(x, y, text::clip(txt, w), t.highlightText, t.surfaceBg, StyleBold);
            return;
        }

        if (sel) {
            const std::u32string head = U">" + text::toUtf32(sel->title);
            const int headLen = std::min(w, static_cast<int>(head.size()));
            screen.putText(x, y, text::clip(head, w), t.highlightText, t.surfaceBg, StyleBold);
            std::u32string rest;
            for (const auto* e : list) {
                if (e == sel || e->startMin / 60 != hour) continue;
                rest += U"; " + text::toUtf32(e->title);
            }
            if (!rest.empty() && headLen < w)
                screen.putText(x + headLen, y, text::clip(rest, w - headLen),
                               t.surfaceText, t.surfaceBg, StyleBold);
            return;
        }

        std::u32string txt;
        bool first = true;
        for (const auto* e : list) {
            if (e->startMin / 60 != hour) continue;
            if (!first) txt += U"; ";
            first = false;
            txt += text::toUtf32(e->title);
        }
        screen.putText(x, y, text::clip(txt, w), t.surfaceText, t.surfaceBg, StyleBold);
    }

    // --- State / navigation ---------------------------------------------------

    // All events overlapping the given hour, sorted by start time (then by id
    // to preserve creation order for ties).
    std::vector<const CalEvent*> slotEvents(int y, int m, int d, int hour) const {
        const int from = hour * 60, to = from + 60;
        std::vector<const CalEvent*> out;
        for (const auto& e : events_)
            if (e.y == y && e.m == m && e.d == d && e.startMin < to && e.endMin > from)
                out.push_back(&e);
        std::sort(out.begin(), out.end(), [](const CalEvent* a, const CalEvent* b) {
            if (a->startMin != b->startMin) return a->startMin < b->startMin;
            return a->id < b->id;
        });
        return out;
    }

    // Selected event in the cursor slot (Tab changes selection); nullptr = none.
    const CalEvent* selectedEvent(int y, int m, int d, int hour) {
        auto list = slotEvents(y, m, d, hour);
        if (list.empty()) return nullptr;
        if (slotSel_ >= static_cast<int>(list.size())) slotSel_ = static_cast<int>(list.size()) - 1;
        if (slotSel_ < 0) slotSel_ = 0;
        return list[static_cast<std::size_t>(slotSel_)];
    }

    bool multiAtCursor() const { // >=2 events in the cursor hour (day/week view)
        return slotEvents(year_, month_, curD_, hourCur_).size() > 1;
    }

    bool hasEvents(int y, int m, int d) const {
        for (const auto& e : events_)
            if (e.y == y && e.m == m && e.d == d) return true;
        return false;
    }

    // Events on the cursor day, sorted by start time (then by id to preserve
    // creation order for ties). Pointers stay valid until events_ mutates.
    std::vector<const CalEvent*> agendaDayEvents() const {
        std::vector<const CalEvent*> list;
        for (const auto& e : events_)
            if (e.y == year_ && e.m == month_ && e.d == curD_) list.push_back(&e);
        std::sort(list.begin(), list.end(), [](const CalEvent* a, const CalEvent* b) {
            if (a->startMin != b->startMin) return a->startMin < b->startMin;
            return a->id < b->id;
        });
        return list;
    }

    // Agenda rows for the SelectionList: one "HH:MM title" row per event.
    std::vector<std::u32string> agendaRows() const {
        std::vector<std::u32string> out;
        for (const auto* e : agendaDayEvents())
            out.push_back(text::toUtf32(hmStr(e->startMin)) + U" " + text::toUtf32(e->title));
        return out;
    }

    // Event behind an agenda row (month view, Tab focus); nullptr = none.
    const CalEvent* agendaEventAt(int row) const {
        auto list = agendaDayEvents();
        if (row < 0 || row >= static_cast<int>(list.size())) return nullptr;
        return list[static_cast<std::size_t>(row)];
    }

    // Scroll the visible hours as needed to keep the cursor hour in view.
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
        resetSlotSelectionIfCursorMoved();
    }

    void resetSlotSelectionIfCursorMoved() {
        if (year_ != selY_ || month_ != selM_ || curD_ != selD_ || hourCur_ != selH_) {
            selY_ = year_;
            selM_ = month_;
            selD_ = curD_;
            selH_ = hourCur_;
            slotSel_ = 0;
        }
    }

    void shiftYear(int dy) {
        year_ = std::clamp(year_ + dy, kMinYear, kMaxYear);
        curD_ = std::min(curD_, daysInMonth(year_, month_));
        resetSlotSelectionIfCursorMoved();
    }

    void prevMonth() {
        if (month_ > 1) --month_;
        else if (year_ > kMinYear) { month_ = 12; --year_; }
        curD_ = std::min(curD_, daysInMonth(year_, month_));
        resetSlotSelectionIfCursorMoved();
    }
    void nextMonth() {
        if (month_ < 12) ++month_;
        else if (year_ < kMaxYear) { month_ = 1; ++year_; }
        curD_ = std::min(curD_, daysInMonth(year_, month_));
        resetSlotSelectionIfCursorMoved();
    }

    // Move the date cursor by delta days (crossing month boundaries as needed).
    void moveCursor(int deltaDays) {
        long days = daysFromCivil(year_, month_, curD_) + deltaDays;
        days = std::clamp(days, daysFromCivil(kMinYear, 1, 1), daysFromCivil(kMaxYear, 12, 31));
        civilFromDays(days, year_, month_, curD_);
        resetSlotSelectionIfCursorMoved();
    }

    // Monday of the cursor's week.
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
            U"January", U"February", U"March", U"April", U"May", U"June",
            U"July", U"August", U"September", U"October", U"November", U"December"};
        return (m >= 1 && m <= 12) ? kMies[m - 1] : U"?";
    }

    static const char32_t* kDni[7]; // defined below (Mon..Sun)

    static bool isLeap(int y) {
        return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    }
    static int daysInMonth(int y, int m) {
        static const int kDim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m < 1 || m > 12) return 0;
        return (m == 2 && isLeap(y)) ? 29 : kDim[m - 1];
    }

    // Days since the epoch (proleptic Gregorian, Hinnant algorithm) —
    // sufficient for weekday calculations, without <chrono> calendar support
    // (portable to older toolchains).
    static long daysFromCivil(int y, int m, int d) {
        y -= m <= 2;
        const long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153u * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2u) / 5u + d - 1u;
        const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
        return era * 146097L + static_cast<long>(doe) - 719468L;
    }

    // Inverse of daysFromCivil (Hinnant): day count -> y/m/d.
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

    // Weekday for any date: 0 = Monday (1970-01-01 = Thursday = 3).
    static int weekdayOf(int y, int m, int d) {
        const long days = daysFromCivil(y, m, d);
        return static_cast<int>(((days + 3) % 7 + 7) % 7);
    }

    // Weekday of the month's first day: 0 = Monday.
    static int weekdayMondayFirst(int y, int m) {
        return weekdayOf(y, m, 1);
    }

    AppContext& ctx_;
    View view_ = View::Month;
    int year_ = 1970;
    int month_ = 1;
    int curD_ = 1;        // date cursor (day)
    int hourCur_ = 0;     // hour cursor (day/week views)
    int hourScroll_ = 0;  // first visible hour row
    std::vector<CalEvent> events_;
    int slotSel_ = 0;                      // selection in the hour slot (Tab)
    int selY_ = -1, selM_ = -1, selD_ = -1, selH_ = -1; // cursor at last selection reset
    std::string draftTitle_, draftStart_, draftEnd_; // kept after validation errors
    int draftEditId_ = -1;                           // edited draft id (-1 = create)
    StatusBar status_;
    SelectionList agenda_; // month view: one row per event on the cursor day
    bool agendaFocus_ = false; // keyboard focus in the agenda (Tab toggles)
    int agendaY_ = -1, agendaM_ = -1, agendaD_ = -1; // agenda day (selection resets on change)
};

inline const char32_t* CalendarView::kDni[7] = {U"Mon", U"Tue", U"Wed", U"Thu", U"Fri", U"Sat", U"Sun"};

inline void CalendarView::newEvent() { eventDialog(nullptr); }

// Shared form for creating (existing = nullptr) and editing an event.
// Restore drafts after validation errors only when the mode matches
// (draftEditId_), so an edit draft is not reused for a new event.
inline void CalendarView::eventDialog(const CalEvent* existing) {
    const int editId = existing ? existing->id : -1;
    if (draftEditId_ != editId) clearDrafts();
    const auto token = lifetimeToken();
    if (draftStart_.empty()) {
        if (existing) {
            draftTitle_ = existing->title;
            draftStart_ = hmStr(existing->startMin);
            draftEnd_ = hmStr(existing->endMin);
        } else {
            // Start = cursor hour (in day/week views), end = one hour later.
            const int sm = hourCur_ * 60;
            draftStart_ = hmStr(sm);
            draftEnd_ = hmStr(std::min(sm + 60, 23 * 60 + 59));
        }
    }
    const int dy = existing ? existing->y : year_;
    const int dm = existing ? existing->m : month_;
    const int dd = existing ? existing->d : curD_;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "Date: %02d.%02d.%04d", dd, dm, dy);
    auto dlg = std::make_unique<Dialog>(text::toUtf32(std::string(buf)),
                                        Dialog::Buttons::OkCancel);
    dlg->addInput(U"Title: ", text::toUtf32(draftTitle_));
    dlg->addInput(U"Start: ", text::toUtf32(draftStart_));
    dlg->addInput(U"End: ", text::toUtf32(draftEnd_));
    Dialog* raw = dlg.get();
    ctx_.openDialog(editId >= 0 ? U"Edit event" : U"Event", std::move(dlg),
                    [this, token, raw, editId, dy, dm, dd](int idx) {
        if (!token.lock()) return;
        if (idx != 0) { clearDrafts(); return; } // Cancel/Esc — start fresh
        const std::string title = text::toUtf8(raw->input(0)->value());
        const std::string sTxt = text::toUtf8(raw->input(1)->value());
        const std::string eTxt = text::toUtf8(raw->input(2)->value());
        int sm = 0, em = 0;
        if (title.empty()) {
            draftTitle_.clear(); draftStart_ = sTxt; draftEnd_ = eTxt; draftEditId_ = editId;
            ctx_.showMessage(U"Enter an event title.");
            return;
        }
        if (!parseHm(sTxt, sm) || !parseHm(eTxt, em)) {
            draftTitle_ = title; draftStart_ = sTxt; draftEnd_ = eTxt; draftEditId_ = editId;
            ctx_.showMessage(U"Time must be in HH:MM format (00:00-23:59).");
            return;
        }
        if (em <= sm) {
            draftTitle_ = title; draftStart_ = sTxt; draftEnd_ = eTxt; draftEditId_ = editId;
            ctx_.showMessage(U"End time must be after start time.");
            return;
        }
        clearDrafts();
        if (editId >= 0) {
            updateEvent(CalEvent{dy, dm, dd, sm, em, title, editId});
            status_.setMessage(text::toUtf32("Updated: " + title));
        } else {
            addEvent(CalEvent{dy, dm, dd, sm, em, title});
            status_.setMessage(text::toUtf32("Added: " + title));
        }
    });
}

// Always confirm deletion — Delete was too easy to press accidentally while
// scrolling through hours.
inline void CalendarView::confirmDelete(const CalEvent& e) {
    const auto token = lifetimeToken();
    const int id = e.id;
    const std::string title = e.title;
    ctx_.confirm(U"Delete event: " + text::toUtf32(title) + U"?",
                 [this, token, id, title](bool ok) {
        if (!token.lock()) return;
        if (!ok) return;
        deleteEvent(id);
        status_.setMessage(text::toUtf32("Deleted: " + title));
    });
}

inline void CalendarView::pollNotifications(AppContext& ctx, bool startup) {
    auto& p = prefs::forApp("calendar");
    const int n = p.getInt("next_id", 0);
    std::vector<CalEvent> events;
    for (int i = 0; i < n; ++i) {
        CalEvent e;
        if (parseEvent(p.get("e" + std::to_string(i)), e)) {
            e.id = i;
            events.push_back(e);
        }
    }

    const time_t now = std::time(nullptr);
    time_t last = static_cast<time_t>(p.getInt("last_notify", 0));
    if (startup || last == 0) last = now - 86400;
    // Throttle background polling to avoid rewriting the prefs file every frame.
    if (!startup && last != 0 && now - last < 30) return;

    for (const auto& e : events) {
        std::tm t{};
        t.tm_year = e.y - 1900;
        t.tm_mon = e.m - 1;
        t.tm_mday = e.d;
        t.tm_hour = e.startMin / 60;
        t.tm_min = e.startMin % 60;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        const time_t ev = std::mktime(&t);
        bool inWindow = false;
        if (startup) inWindow = (ev >= now - 86400 && ev <= now);
        else inWindow = (ev >= last && ev <= now + 60);
        if (!inWindow) continue;
        if (!settings::active.appNotifications) continue;
        const std::string body = e.title + " at " + hmStr(e.startMin);
        ctx.notify("calendar", startup ? "Missed calendar event" : "Calendar event", body);
    }

    p.setInt("last_notify", static_cast<int>(now));
}

class CalendarApp : public App {
public:
    std::string name() const override { return "Calendar"; }

    void onStartup(AppContext& ctx) override {
        CalendarView::pollNotifications(ctx, true);
        ctx.registerBackgroundWorker("Calendar", [](AppContext& c) {
            CalendarView::pollNotifications(c, false);
        });
    }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<CalendarView>(ctx);
        CalendarView* raw = view.get();

        view->setMenuTabs({{U"View", std::vector<MenuItem>{
            {U"Month", [raw] { raw->setView(CalendarView::View::Month); }},
            {U"Week (Ctrl+T)", [raw] { raw->setView(CalendarView::View::Week); }},
            {U"Day (Ctrl+D)", [raw] { raw->setView(CalendarView::View::Day); }},
        }},
        {U"Event", std::vector<MenuItem>{
            {U"New (Ctrl+N)", [raw] { raw->newEvent(); }},
        }}});
        addExitMenu(*view, ctx);
        addAboutMenu(*view, ctx, U"Calendar",
                     U"Plan events in month, week, and day views.");

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newEvent(); });
        view->pushShortcut({true, Key::Char, U't'}, [raw] { raw->setView(CalendarView::View::Week); });
        view->pushShortcut({true, Key::Char, U'd'}, [raw] { raw->setView(CalendarView::View::Day); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        // Wider/taller than before — week view has 7 columns plus hours;
        // 48 columns = 6 per column, enough to fit "Mon 12".
        int w = std::min(48, std::max(30, desktopArea.w - 6));
        int h = std::min(14, std::max(11, desktopArea.h - 4));
        return Rect{desktopArea.x + 4, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir::apps
