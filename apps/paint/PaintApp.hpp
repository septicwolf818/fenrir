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

// Paint — character drawing on a grid (like classic terminal Paint apps).
// The paint cursor moves with the arrow keys; each character is drawn at the
// cursor position and remembered as the brush (Space repeats the brush, Del/eraser clears).
// Ctrl+Z undoes (operation stack), Ctrl+S/O/N save/load/new.
//
// COLORS: every cell has its own fg/bg from the 16-color ANSI palette (0-15) or
// the theme's default color (255 = "like the editor background"). F3 opens a 16x16
// palette (columns = fg, rows = bg, cells drawn with a half-shade ▒),
// arrows select, Enter sets the brush. Saving: inline SGR only on
// color change — a drawing without colors is saved as plain text.
//
// The model (rows_) can be larger than the visible area — then we scroll
// to follow the cursor (like TextArea). The model does NOT shrink when the
// window shrinks, so the drawing survives resizes in both directions.
class PaintView : public Widget {
public:
    explicit PaintView(AppContext& ctx, std::string path = {}) : ctx_(ctx) {
        rows_.assign(1, std::vector<Px>{});
        if (!path.empty()) loadFrom(path);
    }

    // --- Operations from menu / shortcuts (App wires up the callbacks) --------------------

    void newCanvas() {
        guardDirty([this] {
            rows_.assign(1, std::vector<Px>{});
            curX_ = curY_ = scrollX_ = scrollY_ = 0;
            undo_.clear();
            path_.clear();
            dirty_ = false;
            status_.clearMessage();
            palOpen_ = false;
            brush_ = Brush{}; // the brush returns to the default colors
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
        ctx_.saveFile(startDir, "image.fim", [this, token](const std::string& picked) {
            if (!token.lock()) return;
            if (writeTo(picked)) path_ = picked;
        });
    }

    void undoLast() {
        if (undo_.empty()) { status_.setMessage(U"Nothing to undo."); return; }
        const PaintOp u = undo_.back();
        undo_.pop_back();
        if (u.y < static_cast<int>(rows_.size()) && u.x < static_cast<int>(rows_[u.y].size())) {
            rows_[u.y][u.x] = u.px; // restores the character AND both colors
        }
        if (rows_.size() > u.rowCount) rows_.resize(u.rowCount);
        else if (u.y < static_cast<int>(rows_.size()) &&
                 rows_[u.y].size() > u.rowLength) rows_[u.y].resize(u.rowLength);
        dirty_ = true;
        curY_ = u.y;
        curX_ = u.x;
        status_.clearMessage();
    }

    // Opens the color palette (Edit menu). The F3 state also goes through here.
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
        const int viewH = std::max(1, bounds_.h - 1); // last row = status

        ensureVisible(viewW, viewH);

        // Drawing surface background.
        screen.fillRect(Rect{bounds_.x, bounds_.y, viewW, viewH},
                        Cell{U' ', t.editorText, t.editorBg, 0});

        for (int r = 0; r < viewH; ++r) {
            const int row = scrollY_ + r;
            if (row >= static_cast<int>(rows_.size())) break; // model is consistent — empty beyond this
            const std::vector<Px>& line = rows_[row];
            const int end = std::min(scrollX_ + viewW, static_cast<int>(line.size()));
            for (int c = scrollX_; c < end; ++c) {
                const Px& p = line[c];
                screen.set(bounds_.x + (c - scrollX_), bounds_.y + r,
                           Cell{p.ch, resolve(p.fg, t.editorText), resolve(p.bg, t.editorBg), 0});
            }
        }

        // Custom paint cursor (the SELECTED colors swapped) — visible in every
        // mode, not just Edit (the Desktop GUI cursor lives elsewhere).
        if (!palOpen_ && curY_ >= scrollY_ && curY_ < scrollY_ + viewH &&
            curX_ >= scrollX_ && curX_ < scrollX_ + viewW) {
            const Px p = cellAt(curY_, curX_);
            screen.set(bounds_.x + (curX_ - scrollX_), bounds_.y + (curY_ - scrollY_),
                       Cell{p.ch, resolve(p.bg, t.editorBg), resolve(p.fg, t.editorText), StyleBold});
        }

        // Palette ABOVE all content — the selection follows the blinking GUI cursor
        // (Desktop::positionGuiCursor reads cursorPosition()).
        if (palOpen_) drawPalette(screen, viewH);

        status_.setBounds(Rect{bounds_.x, bounds_.y + viewH, bounds_.w, 1});
        std::u32string left = palOpen_
            ? U"Palette: arrows | Enter=ok | Esc=close"
            : U"Arrows=move | Char=paint | Del=eraser | F3=colors";
        std::u32string right;
        if (palOpen_) {
            right = text::toUtf32("fg:" + std::to_string(palFg_) + " bg:" + std::to_string(palBg_));
        } else {
            right = text::toUtf32(std::to_string(curX_ + 1) + ":" + std::to_string(curY_ + 1));
        }
        // StatusBar draws the right label at the very edge — without this the hint
        // and the cursor position would overlap (you'd see a jumble of letters).
        left = text::clip(left, std::max(0, bounds_.w - static_cast<int>(right.size()) - 1));
        status_.setText(std::move(left), std::move(right));
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // F3 toggles the palette; when open — everything goes to the palette
        // (including ctrl+chars, so we don't paint underneath).
        if (ev.key == Key::F3) {
            if (palOpen_) {
                palOpen_ = false;
                return KeyEventResult::Consumed;
            }
            showPalette();
            return KeyEventResult::Consumed;
        }
        if (palOpen_) return handlePaletteKey(ev);

        // Shortcuts (Ctrl+S/O/N/Z) are handled by the Desktop BEFORE the widget;
        // modified characters should never reach the brush as painted text.
        if (ev.key == Key::Char && (ev.ctrl || ev.alt)) return KeyEventResult::Consumed;

        const int viewH = std::max(1, bounds_.h - 1);
        const int viewW = std::max(1, bounds_.w);
        // Like horizontal movement, keep one append position beyond the model.
        // Otherwise painting the last visible row makes Down permanently stop.
        const int maxY = std::max(viewH - 1, static_cast<int>(rows_.size()));

        switch (ev.key) {
            case Key::Up:    curY_ = std::max(0, curY_ - 1); return KeyEventResult::Consumed;
            case Key::Down:  curY_ = std::min(curY_ + 1, maxY); return KeyEventResult::Consumed;
            case Key::Left:  curX_ = std::max(0, curX_ - 1); return KeyEventResult::Consumed;
            case Key::Right: {
                // Free movement right — like downward, independent of the drawn
                // characters: up to the end of the view or the end of the row (the
                // position "past the last character" allows appending). No retreating: when
                // the cursor entered here from a longer row (Down), it doesn't go back.
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
                paint(curY_, curX_, Px{}); // eraser = default cell
                return KeyEventResult::Consumed;
            case Key::Char: {
                if (ev.character < 32) return KeyEventResult::Consumed; // control characters are not painted
                if (ev.character == U' ') {
                    paint(curY_, curX_, Px{brush_.ch, brush_.fg, brush_.bg}); // Space repeats the brush
                } else {
                    paint(curY_, curX_, Px{ev.character, brush_.fg, brush_.bg});
                    brush_.ch = ev.character;
                }
                return KeyEventResult::Consumed;
            }
            default:
                return KeyEventResult::Ignored; // Esc/Tab: Desktop fallbacks
        }
    }

    bool editable() const override { return true; } // characters paint immediately (Edit mode)

    // Unsaved drawing changes — the Desktop asks for confirmation before closing
    // a window with a dirty canvas. Cleared by: New (guardDirty), a successful
    // load (loadFrom) and a successful save (writeTo).
    bool dirty() const override { return dirty_; }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        const int viewH = std::max(1, bounds_.h - 1);
        if (palOpen_) {
            // The blinking GUI cursor marks the selected palette cell.
            const auto [px, py] = palOrigin(viewH);
            const int x = px + palFg_;
            const int y = py + palBg_;
            if (x >= bounds_.x && x < bounds_.x + bounds_.w &&
                y >= bounds_.y && y < bounds_.y + viewH)
                return std::pair<int, int>{x, y};
            return std::nullopt; // defensive (palette clipped) — the status shows fg/bg
        }
        if (curY_ < scrollY_ || curY_ >= scrollY_ + viewH ||
            curX_ < scrollX_ || curX_ >= scrollX_ + bounds_.w) return std::nullopt;
        return std::pair<int, int>{bounds_.x + curX_ - scrollX_, bounds_.y + curY_ - scrollY_};
    }

    // Minimum: a usable drawing surface + status bar.
    int minWidth() const override { return 20; }
    int minHeight() const override { return 6; }

private:
    // Default color = theme color (255 > 15, so it never appears in SGR).
    static constexpr uint8_t kDefColor = 255;
    static constexpr int kPalSize = 16;  // 16x16 palette of fg x bg combinations
    static constexpr int kPalLast = 15;

    struct Px {
        char32_t ch = U' ';
        uint8_t fg = kDefColor;
        uint8_t bg = kDefColor;
        bool operator==(const Px& o) const { return ch == o.ch && fg == o.fg && bg == o.bg; }
        // A purely "empty" cell — used to trim row ends when saving.
        bool isDefault() const { return ch == U' ' && fg == kDefColor && bg == kDefColor; }
    };

    struct Brush {
        char32_t ch = U'#';
        uint8_t fg = kDefColor;
        uint8_t bg = kDefColor;
    };

    struct PaintOp {
        int y;
        int x;
        Px px;
        std::size_t rowCount;
        std::size_t rowLength;
    };

    static Color resolve(uint8_t c, Color def) {
        return c == kDefColor ? def : Color{c};
    }

    // A cell in the model; empty positions use the default (space, theme colors).
    Px cellAt(int y, int x) const {
        if (y < 0 || y >= static_cast<int>(rows_.size())) return Px{};
        const std::vector<Px>& line = rows_[y];
        return x < static_cast<int>(line.size()) ? line[x] : Px{};
    }

    // Row length = the furthest position the cursor can reach (append position).
    int rowLength(int y) const {
        if (y < 0 || y >= static_cast<int>(rows_.size())) return 0;
        return static_cast<int>(rows_[y].size());
    }

    void paint(int y, int x, const Px& val) {
        if (y < 0 || x < 0) return;
        const Px old = cellAt(y, x); // a missing position is a default cell
        if (old == val) return; // nothing changes — do not extend the row
        const std::size_t oldRowCount = rows_.size();
        const std::size_t oldRowLength = y < static_cast<int>(rows_.size())
            ? rows_[y].size() : 0;
        if (y >= static_cast<int>(rows_.size())) rows_.resize(static_cast<std::size_t>(y) + 1);
        std::vector<Px>& line = rows_[y];
        if (x >= static_cast<int>(line.size())) line.resize(static_cast<std::size_t>(x) + 1);
        undo_.push_back({y, x, old, oldRowCount, oldRowLength});
        if (undo_.size() > 1000) undo_.erase(undo_.begin());
        line[x] = val;
        dirty_ = true;
    }

    void ensureVisible(int viewW, int viewH) {
        if (curY_ < scrollY_) scrollY_ = curY_;
        if (viewH > 0 && curY_ >= scrollY_ + viewH) scrollY_ = curY_ - viewH + 1;
        if (curX_ < scrollX_) scrollX_ = curX_;
        if (viewW > 0 && curX_ >= scrollX_ + viewW) scrollX_ = curX_ - viewW + 1;
        scrollY_ = std::max(0, scrollY_);
        scrollX_ = std::max(0, scrollX_);
    }

    // --- 16x16 palette ----------------------------------------------------------

    // Palette origin (centered in the drawing area). In small windows, scroll
    // the palette so the selected cell remains visible in either direction.
    std::pair<int, int> palOrigin(int viewH) const {
        const int xOffset = std::min(palFg_, std::max(0, kPalSize - bounds_.w));
        const int px = bounds_.x + std::max(0, (bounds_.w - kPalSize) / 2) - xOffset;
        int py = bounds_.y + std::max(0, (viewH - kPalSize) / 2);
        if (viewH < kPalSize) py = bounds_.y - std::min(palBg_, kPalSize - viewH);
        return {px, py};
    }

    void drawPalette(Screen& screen, int viewH) const {
        const auto& t = theme::active;
        const auto [px, py] = palOrigin(viewH);
        const int x0 = std::max(px, bounds_.x);
        const int x1 = std::min(px + kPalSize, bounds_.x + bounds_.w);
        const int y0 = std::max(py, bounds_.y); // visible portion of the palette
        const int y1 = std::min(py + kPalSize, bounds_.y + viewH);
        if (x1 <= x0 || y1 <= y0) return;
        screen.fillRect(Rect{x0, y0, x1 - x0, y1 - y0}, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        for (int bg = 0; bg < kPalSize; ++bg) {
            for (int fg = 0; fg < kPalSize; ++fg) {
                const int x = px + fg;
                const int y = py + bg;
                if (x < bounds_.x || x >= bounds_.x + bounds_.w) continue;
                if (y < bounds_.y || y >= bounds_.y + viewH) continue;
                // Draw cells with a half-shade block so both colors are visible.
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
            case Key::Enter:   // confirm — apply the selected color pair to the brush
                brush_.fg = static_cast<uint8_t>(palFg_);
                brush_.bg = static_cast<uint8_t>(palBg_);
                palOpen_ = false;
                status_.clearMessage();
                return KeyEventResult::Consumed;
            case Key::Escape:  // cancel — leave the brush unchanged
                palOpen_ = false;
                return KeyEventResult::Consumed;
            default:
                return KeyEventResult::Consumed; // palette open — nothing underneath
        }
    }

    // --- Save / load (inline SGR, only when the color changes) ------------

    static std::string fgCode(uint8_t c) {
        if (c == kDefColor) return "39";
        return std::to_string(c < 8 ? 30 + c : 90 + (c - 8));
    }
    static std::string bgCode(uint8_t c) {
        if (c == kDefColor) return "49";
        return std::to_string(c < 8 ? 40 + c : 100 + (c - 8));
    }

    // Save: trim trailing default cells; emit colors as SGR only when they
    // change, so drawings without colors are saved as plain text (compatible
    // with older files). A colored trailing space is ART and is not trimmed.
    bool writeTo(const std::string& path) {
        std::ofstream f(path, std::ios::trunc);
        if (!f) { status_.setMessage(text::toUtf32("Cannot save: " + path)); return false; }
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
        f.flush();
        f.close();
        if (!f) { status_.setMessage(text::toUtf32("Save failed: " + path)); return false; }
        dirty_ = false;
        status_.setMessage(text::toUtf32("Saved: " + fs::nameOf(path)));
        return true;
    }

    // Parse one file row: text fragments plus SGR sequences.
    // Supported: 0 (reset), 30-37/90-97 (fg), 40-47/100-107 (bg), 39/49.
    // Other codes (bold, etc.) are ignored; characters without SGR use the default color.
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
                if (j >= raw.size()) break; // incomplete sequence — end of row
                applySgr(raw.substr(i + 2, j - (i + 2)), fg, bg);
                i = j + 1;
            } else {
                ++i; // bare ESC (legacy file) — skip it
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
            if (!any) code = 0; // empty parameter = default (as in a terminal)
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
        if (!f) { status_.setMessage(text::toUtf32("Cannot open: " + path)); return; }
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
        status_.setMessage(text::toUtf32("Loaded: " + fs::nameOf(path)));
    }

    // As in the editor: ask before discarding changes to a modified file.
    void guardDirty(std::function<void()> proceed) {
        if (!dirty_) { proceed(); return; }
        const auto token = lifetimeToken();
        ctx_.confirm(U"Unsaved changes. Continue?",
                     [token, proceed = std::move(proceed)](bool ok) {
                         if (!token.lock()) return;
                         if (ok) proceed();
                     });
    }

    AppContext& ctx_;
    std::vector<std::vector<Px>> rows_; // drawing model (may exceed the visible area)
    std::vector<PaintOp> undo_;         // undo stack, capped at 1000
    int curX_ = 0, curY_ = 0;           // paint cursor (model coordinates)
    int scrollX_ = 0, scrollY_ = 0;     // view offset following the cursor
    Brush brush_;                       // brush: character + color pair
    bool palOpen_ = false;              // is the palette open?
    int palFg_ = 7, palBg_ = 0;         // selected palette column/row (0..15)
    bool dirty_ = false;
    std::string path_;                  // "" = not saved yet
    StatusBar status_;
};

class PaintApp : public App {
public:
    std::string name() const override { return "Paint"; }
    std::string id() const override { return "paint"; }
    std::vector<std::string> fileExtensions() const override { return {"fim"}; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        return makeView(ctx, std::string{});
    }

    std::unique_ptr<Widget> createFileView(AppContext& ctx, const std::string& path) override {
        // Same menus and shortcuts as a normal window — only the canvas
        // already has the file loaded.
        return makeView(ctx, path);
    }

    std::unique_ptr<Widget> makeView(AppContext& ctx, std::string path) {
        auto view = std::make_unique<PaintView>(ctx, std::move(path));
        PaintView* raw = view.get();

        view->setMenuTabs({{U"File", std::vector<MenuItem>{
            {U"New (Ctrl+N)", [raw] { raw->newCanvas(); }},
            {U"Open... (Ctrl+O)", [raw] { raw->openFile(); }},
            {U"Save (Ctrl+S)", [raw] { raw->save(); }},
            {U"Save As...", [raw] { raw->saveAs(); }},
        }},
        {U"Edit", std::vector<MenuItem>{
            {U"Undo (Ctrl+Z)", [raw] { raw->undoLast(); }},
            {U"Colors (F3)", [raw] { raw->showPalette(); }},
        }}});
        addExitMenu(*view, ctx);
        addAboutMenu(*view, ctx, U"Paint",
                     U"Draw character-cell images using the 16-color ANSI palette.");

        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->newCanvas(); });
        view->pushShortcut({true, Key::Char, U'o'}, [raw] { raw->openFile(); });
        view->pushShortcut({true, Key::Char, U's'}, [raw] { raw->save(); });
        view->pushShortcut({true, Key::Char, U'z'}, [raw] { raw->undoLast(); });
        view->pushShortcut({false, Key::F3, std::nullopt, false}, [raw] { raw->showPalette(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        int w = std::min(60, std::max(22, desktopArea.w - 6));
        // Height 20 = 16 palette rows + status bar + window border and menu bar
        // (content = h - 3, so h = 20 fits the full palette).
        // Keep the regular window centered and avoid unnecessary overflow on
        // narrow/short terminals. Window chrome adds 2 columns and 3 rows.
        int h = std::min(20, std::max(9, desktopArea.h - 4));
        const int x = desktopArea.x + std::max(0, (desktopArea.w - w) / 2);
        const int y = desktopArea.y + std::max(0, (desktopArea.h - h) / 2);
        return Rect{x, y, w, h};
    }
};

} // namespace fenrir::apps
