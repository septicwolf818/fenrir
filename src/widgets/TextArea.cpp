#include "fenrir/widgets/TextArea.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"

namespace fenrir {

TextArea::TextArea() { editable_ = true; }

// --- Layout (logical lines -> visual rows) -----------------------------------

int TextArea::gutterW() const {
    if (!lineNumbers_) return 0;
    int n = static_cast<int>(lines_.size());
    int digits = 1;
    while (n >= 10) { n /= 10; ++digits; }
    return digits + 1; // number + one space of padding
}

void TextArea::rebuildLayout() const {
    layoutW_ = std::max(1, bounds_.w - gutterW());
    layout_.clear();

    for (int li = 0; li < static_cast<int>(lines_.size()); ++li) {
        const std::u32string& line = lines_[static_cast<std::size_t>(li)];
        if (line.empty()) {
            layout_.push_back(Seg{li, 0, 0}); // an empty line = one empty visual row
            continue;
        }
        if (!wrap_) {
            // Without wrapping: the whole logical line is one visual row
            // (if wider than the screen, drawing scrolls it horizontally via scrollCol_).
            layout_.push_back(Seg{li, 0, static_cast<int>(line.size())});
            continue;
        }
        int pos = 0;
        const int n = static_cast<int>(line.size());
        while (pos < n) {
            const int remain = n - pos;
            if (remain <= layoutW_) {
                layout_.push_back(Seg{li, pos, remain});
                break;
            }
            // Break at the last space in the window; keep the space at the end
            // of the row (so segments remain contiguous and each character appears once).
            int brk = -1;
            for (int k = pos + layoutW_ - 1; k > pos; --k) {
                if (line[static_cast<std::size_t>(k)] == U' ') { brk = k; break; }
            }
            const int len = (brk > pos) ? (brk - pos + 1) : layoutW_; // no space = hard break
            layout_.push_back(Seg{li, pos, len});
            pos += len;
        }
    }
    if (layout_.empty()) layout_.push_back(Seg{0, 0, 0});
    layoutDirty_ = false;
}

void TextArea::ensureLayout() const {
    if (!layoutDirty_ && layoutW_ == std::max(1, bounds_.w - gutterW())) return;
    rebuildLayout();
}

int TextArea::visualRowOf(int row, int col) const {
    // Return the visual row index for (row, col). A column at or past the end
    // of a line maps to the LAST visual row of that line.
    int found = 0;
    for (int i = 0; i < static_cast<int>(layout_.size()); ++i) {
        const Seg& s = layout_[static_cast<std::size_t>(i)];
        if (s.line == row) {
            found = i;
            if (col < s.col0 + s.len) return i;
        } else if (s.line > row) {
            break;
        }
    }
    return found;
}

std::pair<int, int> TextArea::logicalOf(int segIdx, int offset) const {
    segIdx = std::clamp(segIdx, 0, static_cast<int>(layout_.size()) - 1);
    const Seg& s = layout_[static_cast<std::size_t>(segIdx)];
    return {s.line, s.col0 + std::clamp(offset, 0, s.len)};
}

int TextArea::visualOffset() const {
    const Seg& s = layout_[static_cast<std::size_t>(visualRowOf(cursorRow_, cursorCol_))];
    return std::clamp(cursorCol_ - s.col0, 0, s.len);
}

void TextArea::syncGoal() {
    ensureLayout();
    goalCol_ = visualOffset();
}

// --- Editing -----------------------------------------------------------------

void TextArea::loadFile(const std::string& path) {
    path_ = path;
    std::ifstream f(path, std::ios::binary);
    lines_.clear();
    if (!f) {
        lines_.push_back(std::u32string());
        dirty_ = false;
        markDirty();
        if (onStatus) onStatus("New file: " + path);
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::u32string content = text::toUtf32(raw);
    std::u32string current;
    for (char32_t c : content) {
        if (c == U'\n') {
            if (!current.empty() && current.back() == U'\r') current.pop_back();
            lines_.push_back(std::move(current));
            current.clear();
        } else {
            current += c;
        }
    }
    lines_.push_back(std::move(current));
    if (lines_.empty()) lines_.push_back(std::u32string());

    cursorRow_ = 0;
    cursorCol_ = 0;
    scrollRow_ = 0;
    scrollCol_ = 0;
    goalCol_ = 0;
    dirty_ = false;
    markDirty();
    if (onStatus) onStatus("Loaded: " + path + " (" + std::to_string(lines_.size()) + " lines)");
}

void TextArea::saveFile(const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (onStatus) onStatus("Save failed: " + path);
        return;
    }
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        f << text::toUtf8(lines_[i]);
        if (i + 1 < lines_.size()) f << '\n';
        if (!f) {
            if (onStatus) onStatus("Save failed: " + path);
            return;
        }
    }
    f.flush();
    if (!f) {
        if (onStatus) onStatus("Save failed: " + path);
        return;
    }
    f.close();
    if (f.fail()) {
        if (onStatus) onStatus("Save failed: " + path);
        return;
    }
    path_ = path;
    dirty_ = false;
    if (onStatus) onStatus("Saved: " + path);
}

void TextArea::saveToCurrentPath() {
    if (path_.empty()) {
        if (onStatus) onStatus("No file name - use saveFile(path)");
        return;
    }
    saveFile(path_);
}

void TextArea::newFile() {
    lines_.clear();
    lines_.push_back(std::u32string());
    cursorRow_ = 0;
    cursorCol_ = 0;
    scrollRow_ = 0;
    scrollCol_ = 0;
    goalCol_ = 0;
    path_.clear();
    dirty_ = false;
    markDirty();
    if (onStatus) onStatus("New file");
}

void TextArea::clearText() {
    lines_.clear();
    lines_.push_back(std::u32string());
    cursorRow_ = 0;
    cursorCol_ = 0;
    scrollRow_ = 0;
    scrollCol_ = 0;
    goalCol_ = 0;
    dirty_ = true;
    markDirty();
    if (onStatus) onStatus("Cleared");
}

void TextArea::insertChar(char32_t ch) {
    lines_[cursorRow_].insert(lines_[cursorRow_].begin() + cursorCol_, ch);
    ++cursorCol_;
    dirty_ = true;
    markDirty();
    syncGoal();
}

void TextArea::insertNewline() {
    std::u32string rest = lines_[cursorRow_].substr(cursorCol_);
    lines_[cursorRow_].resize(cursorCol_);
    lines_.insert(lines_.begin() + cursorRow_ + 1, rest);
    ++cursorRow_;
    cursorCol_ = 0;
    dirty_ = true;
    markDirty();
    syncGoal();
}

void TextArea::backspace() {
    if (cursorCol_ > 0) {
        lines_[cursorRow_].erase(lines_[cursorRow_].begin() + cursorCol_ - 1);
        --cursorCol_;
        dirty_ = true;
        markDirty();
        syncGoal();
    } else if (cursorRow_ > 0) {
        int prevLen = static_cast<int>(lines_[cursorRow_ - 1].size());
        lines_[cursorRow_ - 1] += lines_[cursorRow_];
        lines_.erase(lines_.begin() + cursorRow_);
        --cursorRow_;
        cursorCol_ = prevLen;
        dirty_ = true;
        markDirty();
        syncGoal();
    }
}

void TextArea::del() {
    if (cursorCol_ < static_cast<int>(lines_[cursorRow_].size())) {
        lines_[cursorRow_].erase(lines_[cursorRow_].begin() + cursorCol_);
        dirty_ = true;
        markDirty();
        syncGoal();
    } else if (cursorRow_ + 1 < static_cast<int>(lines_.size())) {
        lines_[cursorRow_] += lines_[cursorRow_ + 1];
        lines_.erase(lines_.begin() + cursorRow_ + 1);
        dirty_ = true;
        markDirty();
        syncGoal();
    }
}

// --- Cursor movement ---------------------------------------------------------

void TextArea::clampCursor() {
    cursorRow_ = std::clamp(cursorRow_, 0, static_cast<int>(lines_.size()) - 1);
    cursorCol_ = std::clamp(cursorCol_, 0, static_cast<int>(lines_[cursorRow_].size()));
}

void TextArea::moveHoriz(int dCol) {
    ensureLayout();
    if (dCol < 0) {
        if (cursorCol_ > 0) {
            --cursorCol_;
        } else if (cursorRow_ > 0) {
            --cursorRow_;
            cursorCol_ = static_cast<int>(lines_[cursorRow_].size());
        }
    } else if (dCol > 0) {
        if (cursorCol_ < static_cast<int>(lines_[cursorRow_].size())) {
            ++cursorCol_;
        } else if (cursorRow_ + 1 < static_cast<int>(lines_.size())) {
            ++cursorRow_;
            cursorCol_ = 0;
        }
    }
    syncGoal();
    ensureVisible();
}

void TextArea::moveVert(int dSeg) {
    ensureLayout();
    const int cur = visualRowOf(cursorRow_, cursorCol_);
    const int target = std::clamp(cur + dSeg, 0, static_cast<int>(layout_.size()) - 1);
    auto [row, col] = logicalOf(target, goalCol_); // sticky goal: the same visual offset
    cursorRow_ = row;
    cursorCol_ = col;
    ensureVisible();
}

void TextArea::ensureVisible() {
    ensureLayout();
    const int visible = std::max(1, bounds_.h);
    const int seg = visualRowOf(cursorRow_, cursorCol_);
    if (seg < scrollRow_) scrollRow_ = seg;
    if (seg >= scrollRow_ + visible) scrollRow_ = seg - visible + 1;
    scrollRow_ = std::clamp(scrollRow_, 0, std::max(0, static_cast<int>(layout_.size()) - 1));

    // Horizontally only when wrapping is disabled (wrapped rows never exceed
    // the width, so scrollCol_ is always 0).
    if (wrap_) {
        scrollCol_ = 0;
        return;
    }
    const int off = visualOffset(); // cursor column in its visual row
    const int textW = std::max(1, bounds_.w - gutterW());
    if (off < scrollCol_) scrollCol_ = off;
    if (off >= scrollCol_ + textW) scrollCol_ = off - textW + 1;
    scrollCol_ = std::max(0, scrollCol_);
}

// --- Keyboard ----------------------------------------------------------------

KeyEventResult TextArea::handleKey(const KeyEvent& ev) {
    if (ev.ctrl && ev.key == Key::Char && (ev.character == U's' || ev.character == U'S')) {
        if (onSaveRequested) onSaveRequested();
        else saveToCurrentPath();
        return KeyEventResult::Consumed;
    }
    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'o' || ev.character == U'O')) {
        if (onOpenRequested) onOpenRequested();
        return KeyEventResult::Consumed;
    }
    // Ctrl+E toggles line wrapping (on by default; off enables horizontal scrolling).
    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'e' || ev.character == U'E')) {
        toggleWrap();
        return KeyEventResult::Consumed;
    }
    switch (ev.key) {
        case Key::Up:    moveVert(-1); return KeyEventResult::Consumed;
        case Key::Down:  moveVert(+1); return KeyEventResult::Consumed;
        case Key::Left:  moveHoriz(-1); return KeyEventResult::Consumed;
        case Key::Right: moveHoriz(+1); return KeyEventResult::Consumed;
        case Key::Home: { // Home = start of the current visual row
            ensureLayout();
            cursorCol_ = layout_[static_cast<std::size_t>(visualRowOf(cursorRow_, cursorCol_))].col0;
            syncGoal();
            ensureVisible(); // also scrolls to column 0 when wrapping is disabled
            return KeyEventResult::Consumed;
        }
        case Key::End: { // End = end of the current visual row
            ensureLayout();
            const Seg& s = layout_[static_cast<std::size_t>(visualRowOf(cursorRow_, cursorCol_))];
            cursorCol_ = s.col0 + s.len;
            syncGoal();
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        case Key::PageUp:   moveVert(-std::max(1, bounds_.h)); return KeyEventResult::Consumed;
        case Key::PageDown: moveVert(+std::max(1, bounds_.h)); return KeyEventResult::Consumed;
        case Key::Enter:    insertNewline(); ensureVisible(); return KeyEventResult::Consumed;
        case Key::Backspace: backspace(); ensureVisible(); return KeyEventResult::Consumed;
        case Key::Delete:   del(); ensureVisible(); return KeyEventResult::Consumed;
        case Key::Tab:
            // Pass modified Tab (Alt+Tab/Ctrl+Tab switches windows globally)
            // through to the Desktop; plain Tab inserts a tab character or
            // spaces (setInsertsSpaces).
            if (ev.ctrl || ev.alt) return KeyEventResult::Ignored;
            if (insertsSpaces_) {
                const int n = 4 - (cursorCol_ % 4);
                for (int i = 0; i < n; ++i) insertChar(U' ');
            } else {
                insertChar(U'\t');
            }
            ensureVisible();
            return KeyEventResult::Consumed;
        case Key::Char:
            if (!ev.ctrl && !ev.alt) { insertChar(ev.character); ensureVisible(); return KeyEventResult::Consumed; }
            return KeyEventResult::Ignored;
        default: return KeyEventResult::Ignored;
    }
}

// --- Drawing -----------------------------------------------------------------

void TextArea::draw(Screen& screen) {
    ensureLayout();
    const auto& t = theme::active;
    const Cell fill{U' ', t.editorText, t.editorBg, 0};
    const int gut = gutterW();
    const int textW = std::max(1, bounds_.w - gut);

    for (int row = 0; row < bounds_.h; ++row) {
        screen.fillRect(Rect{bounds_.x, bounds_.y + row, bounds_.w, 1}, fill);
        const int segIdx = scrollRow_ + row;
        if (segIdx >= static_cast<int>(layout_.size())) continue; // beyond the text — leave the background
        const Seg& s = layout_[static_cast<std::size_t>(segIdx)];
        const std::u32string& line = lines_[static_cast<std::size_t>(s.line)];

        // Gutter: logical line number on the FIRST visual row
        // (continuation rows remain blank when wrapping). Right-aligned + space.
        if (gut > 0) {
            const bool firstRow =
                segIdx == 0 || layout_[static_cast<std::size_t>(segIdx - 1)].line != s.line;
            if (firstRow) {
                std::u32string num = text::toUtf32(std::to_string(s.line + 1));
                if (static_cast<int>(num.size()) > gut - 1)
                    num = num.substr(num.size() - static_cast<std::size_t>(gut - 1));
                const int pad = gut - 1 - static_cast<int>(num.size());
                screen.putText(bounds_.x + pad, bounds_.y + row, num, t.dimText, t.editorBg);
            }
        }

        if (s.len <= 0) continue;
        int start = s.col0;
        int len = s.len;
        if (!wrap_) { // row wider than the screen — shift the start by scrollCol_
            start += scrollCol_;
            len -= scrollCol_;
            if (len <= 0) continue;
        }
        const int n = std::min(len, textW);
        // Control characters in the buffer (e.g. '\t' from a loaded file) must
        // be replaced BEFORE drawing; otherwise they reach the terminal as raw
        // bytes and disrupt cursor positioning. Tabs are shown as '→'.
        std::u32string cell(line.substr(static_cast<std::size_t>(start),
                                        static_cast<std::size_t>(n)));
        for (char32_t& c : cell)
            if (c == U'\t') c = U'→';
        screen.putText(bounds_.x + gut, bounds_.y + row, cell, t.editorText, t.editorBg);
    }
}

std::optional<std::pair<int, int>> TextArea::cursorPosition() const {
    ensureLayout();
    const int seg = visualRowOf(cursorRow_, cursorCol_);
    const int screenRow = seg - scrollRow_;
    const int gut = gutterW();
    const int textW = std::max(1, bounds_.w - gut);
    if (screenRow < 0 || screenRow >= bounds_.h || bounds_.w <= 0) return std::nullopt;
    int offset = visualOffset();
    if (!wrap_) offset -= scrollCol_; // screen position (the column may be scrolled)
    if (offset < 0) offset = 0;
    if (offset >= textW) offset = textW - 1; // end of a full visual row
    return std::make_pair(bounds_.x + gut + offset, bounds_.y + screenRow);
}

} // namespace fenrir
