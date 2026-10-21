#include "fenrir/widgets/TextArea.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "fenrir/core/Clipboard.hpp"
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

// --- Selection ---------------------------------------------------------------

std::tuple<int, int, int, int> TextArea::selectionRange() const {
    int sr = selRow_, sc = selCol_, er = cursorRow_, ec = cursorCol_;
    if (sr > er || (sr == er && sc > ec)) {
        std::swap(sr, er);
        std::swap(sc, ec);
    }
    return {sr, sc, er, ec};
}

bool TextArea::isSelected(int line, int col) const {
    if (!selecting_) return false;
    const auto [sr, sc, er, ec] = selectionRange();
    if (line < sr || line > er) return false;
    if (line == sr && col < sc) return false;
    if (line == er && col >= ec) return false;
    return true;
}

std::u32string TextArea::selectedText() const {
    if (!selecting_) return {};
    const auto [sr, sc, er, ec] = selectionRange();
    if (sr == er && sc >= ec) return {};
    std::u32string out;
    for (int r = sr; r <= er && r < static_cast<int>(lines_.size()); ++r) {
        const std::u32string& line = lines_[static_cast<std::size_t>(r)];
        const int from = (r == sr) ? sc : 0;
        const int to = (r == er) ? ec : static_cast<int>(line.size());
        if (to > from) out += line.substr(static_cast<std::size_t>(from),
                                          static_cast<std::size_t>(to - from));
        if (r != er) out += U'\n';
    }
    return out;
}

void TextArea::selectAll() {
    selRow_ = 0;
    selCol_ = 0;
    selecting_ = true;
    // The anchor stays at the start; the cursor marks the end.
    const int last = static_cast<int>(lines_.size()) - 1;
    cursorRow_ = std::max(0, last);
    cursorCol_ = static_cast<int>(lines_[static_cast<std::size_t>(cursorRow_)].size());
}

void TextArea::clearSelection() {
    selecting_ = false;
    selRow_ = cursorRow_;
    selCol_ = cursorCol_;
}

void TextArea::beginSelection() {
    if (selecting_) return; // keep the original anchor while extending
    selRow_ = cursorRow_;
    selCol_ = cursorCol_;
    selecting_ = true;
}

void TextArea::deleteSelection() {
    if (!selecting_) return;
    const auto [sr, sc, er, ec] = selectionRange();
    if (sr == er && sc >= ec) {
        selecting_ = false;
        return;
    }
    if (er >= static_cast<int>(lines_.size())) {
        selecting_ = false;
        return;
    }
    std::u32string& first = lines_[static_cast<std::size_t>(sr)];
    const std::u32string& last = lines_[static_cast<std::size_t>(er)];
    first = first.substr(0, static_cast<std::size_t>(sc)) + last.substr(static_cast<std::size_t>(ec));
    lines_.erase(lines_.begin() + sr + 1, lines_.begin() + er + 1);
    cursorRow_ = sr;
    cursorCol_ = sc;
    selecting_ = false;
    selRow_ = sr;
    selCol_ = sc;
    dirty_ = true;
    markDirty();
    clampCursor();
    ensureVisible();
}

bool TextArea::copySelection() {
    if (!selecting_) return false;
    const std::u32string sel = selectedText();
    if (sel.empty()) return false;
    clipboard::setText(text::toUtf8(sel));
    if (onStatus) onStatus("Copied " + std::to_string(sel.size()) + " characters");
    return true;
}

bool TextArea::cutSelection() {
    if (!copySelection()) return false;
    deleteSelection();
    if (onStatus) onStatus("Cut");
    return true;
}

void TextArea::pasteClipboard() {
    const std::string utf8 = clipboard::text();
    if (utf8.empty()) {
        if (onStatus) onStatus("Clipboard is empty");
        return;
    }
    if (selecting_) deleteSelection();
    insertText(text::toUtf32(utf8));
    ensureVisible();
    if (onStatus) onStatus("Pasted " + std::to_string(utf8.size()) + " characters");
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
        // A file that does not exist yet: remember "missing" so a later
        // external change (created or written by someone else) is noticed.
        fileStamp_ = FileWatchdog::State{};
        fileSavedAt_ = std::time(nullptr);
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
    // Remember the state of the file on disk: everything after this point is
    // compared against it to notice changes made outside the editor.
    fileStamp_ = FileWatchdog::stateOf(path);
    fileSavedAt_ = std::time(nullptr);
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
    // The file now holds our content — the new state is the reference for
    // later external changes.
    fileStamp_ = FileWatchdog::stateOf(path);
    fileSavedAt_ = std::time(nullptr);
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
    fileStamp_ = FileWatchdog::State{};
    fileSavedAt_ = 0;
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

void TextArea::insertText(const std::u32string& text) {
    if (text.empty()) return;
    std::u32string chunk;
    for (const char32_t ch : text) {
        if (ch == U'\n') {
            if (!chunk.empty()) {
                std::u32string& line = lines_[static_cast<std::size_t>(cursorRow_)];
                line.insert(line.begin() + cursorCol_, chunk.begin(), chunk.end());
                cursorCol_ += static_cast<int>(chunk.size());
                chunk.clear();
            }
            insertNewline();
        } else if (ch == U'\r') {
            // CR of a CRLF pair — the LF does the work.
        } else {
            chunk.push_back(ch);
        }
    }
    if (!chunk.empty()) {
        std::u32string& line = lines_[static_cast<std::size_t>(cursorRow_)];
        line.insert(line.begin() + cursorCol_, chunk.begin(), chunk.end());
        cursorCol_ += static_cast<int>(chunk.size());
    }
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

void TextArea::setCursorLineCol(int line, int col) {
    if (lines_.empty()) return;
    cursorRow_ = std::clamp(line - 1, 0, static_cast<int>(lines_.size()) - 1);
    cursorCol_ = std::clamp(col - 1, 0, static_cast<int>(lines_[static_cast<std::size_t>(cursorRow_)].size()));
    goalCol_ = 0; // the sticky column belongs to the old content
    markDirty();
    ensureVisible();
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
    // Clipboard: copy / cut / paste / select all. They work on the selection
    // made with Shift+Arrow (and on the whole buffer for select all).
    if (ev.ctrl && ev.key == Key::Char) {
        switch (ev.character) {
            case U'c': case U'C':
                copySelection();
                return KeyEventResult::Consumed;
            case U'x': case U'X':
                cutSelection();
                return KeyEventResult::Consumed;
            case U'v': case U'V':
                pasteClipboard();
                return KeyEventResult::Consumed;
            case U'a': case U'A':
                selectAll();
                ensureVisible();
                return KeyEventResult::Consumed;
            default: break;
        }
    }
    // Shift+Arrow extends the selection; a plain cursor move drops it.
    const bool moveKey = ev.key == Key::Up || ev.key == Key::Down || ev.key == Key::Left ||
                         ev.key == Key::Right || ev.key == Key::Home || ev.key == Key::End ||
                         ev.key == Key::PageUp || ev.key == Key::PageDown;
    if (moveKey) {
        if (ev.shift) beginSelection();
        else clearSelection();
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
        case Key::Enter:
            if (selecting_) deleteSelection();
            insertNewline();
            ensureVisible();
            return KeyEventResult::Consumed;
        case Key::Backspace:
            if (selecting_) {
                deleteSelection();
            } else {
                backspace();
            }
            ensureVisible();
            return KeyEventResult::Consumed;
        case Key::Delete:
            if (selecting_) {
                deleteSelection();
            } else {
                del();
            }
            ensureVisible();
            return KeyEventResult::Consumed;
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
            if (!ev.ctrl && !ev.alt) {
                if (selecting_) deleteSelection(); // typing replaces the selection
                insertChar(ev.character);
                ensureVisible();
                return KeyEventResult::Consumed;
            }
            return KeyEventResult::Ignored;
        default: return KeyEventResult::Ignored;
    }
}

// --- Drawing -----------------------------------------------------------------

void TextArea::ensureBlockStates() const {
    if (blockValid_ || !blockScan_) return;
    blockStart_ = blockScan_(lines_);
    blockValid_ = true;
}

void TextArea::draw(Screen& screen) {
    ensureLayout();
    ensureBlockStates();
    const auto& t = theme::active;
    const Cell fill{U' ', t.editorText, t.editorBg, 0};
    const int gut = gutterW();
    const int textW = std::max(1, bounds_.w - gut);

    // Paint the whole area's background up front — every row, every column,
    // including rows past the end of the buffer. Nothing from a previous frame
    // (or a wider line that used to sit on a row) can survive where the current
    // line has no characters.
    screen.fillRect(bounds_, fill);

    for (int row = 0; row < bounds_.h; ++row) {
        const int segIdx = scrollRow_ + row;
        if (segIdx < 0 || segIdx >= static_cast<int>(layout_.size())) continue; // beyond the text — background already painted
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
        if (syntaxHighlighter_) {
            std::vector<SyntaxSpan> spans;
            const std::size_t li = static_cast<std::size_t>(s.line);
            const bool inBlock =
                blockValid_ && li < blockStart_.size() && blockStart_[li] != 0;
            syntaxHighlighter_(line, inBlock, spans);
            std::size_t spanIndex = 0;
            for (int i = 0; i < n; ++i) {
                const std::size_t logicalCol = static_cast<std::size_t>(start + i);
                while (spanIndex < spans.size() && spans[spanIndex].end <= logicalCol)
                    ++spanIndex;

                Color fg = t.editorText;
                uint8_t style = StyleNone;
                if (spanIndex < spans.size() && spans[spanIndex].begin <= logicalCol &&
                    logicalCol < spans[spanIndex].end) {
                    switch (spans[spanIndex].token) {
                        case SyntaxToken::Identifier: break;
                        case SyntaxToken::Keyword: fg = t.syntaxKeyword; style = StyleBold; break;
                        case SyntaxToken::Type: fg = t.syntaxType; style = StyleBold; break;
                        case SyntaxToken::String: fg = t.syntaxString; break;
                        case SyntaxToken::Number: fg = t.syntaxNumber; break;
                        case SyntaxToken::Comment: fg = t.syntaxComment; break;
                        case SyntaxToken::Directive: fg = t.syntaxDirective; style = StyleBold; break;
                    }
                }
                char32_t ch = line[logicalCol];
                if (ch == U'\t') ch = U'→';
                Color cellFg = fg;
                Color cellBg = t.editorBg;
                if (isSelected(s.line, static_cast<int>(logicalCol))) {
                    // The selection is drawn in the theme's accent colors.
                    cellFg = t.accentText;
                    cellBg = t.accentBg;
                }
                screen.set(bounds_.x + gut + i, bounds_.y + row,
                           Cell{ch, cellFg, cellBg, style});
            }
            continue;
        }

        // Control characters in the buffer (e.g. '\t' from a loaded file) must
        // be replaced BEFORE drawing; otherwise they reach the terminal as raw
        // bytes and disrupt cursor positioning. Tabs are shown as '→'.
        std::u32string cell(line.substr(static_cast<std::size_t>(start),
                                        static_cast<std::size_t>(n)));
        for (char32_t& c : cell)
            if (c == U'\t') c = U'→';
        // Draw the row in up to three parts: before the selection, the
        // selection itself (accent colors) and the rest.
        if (!selecting_) {
            screen.putText(bounds_.x + gut, bounds_.y + row, cell, t.editorText, t.editorBg);
            continue;
        }
        int selFrom = -1, selTo = -1;
        for (int i = 0; i < n; ++i) {
            const int logicalCol = start + i;
            if (isSelected(s.line, logicalCol)) {
                if (selFrom < 0) selFrom = i;
                selTo = i + 1;
            }
        }
        if (selFrom < 0) {
            screen.putText(bounds_.x + gut, bounds_.y + row, cell, t.editorText, t.editorBg);
            continue;
        }
        if (selFrom > 0)
            screen.putText(bounds_.x + gut, bounds_.y + row,
                           cell.substr(0, static_cast<std::size_t>(selFrom)),
                           t.editorText, t.editorBg);
        screen.putText(bounds_.x + gut + selFrom, bounds_.y + row,
                       cell.substr(static_cast<std::size_t>(selFrom),
                                   static_cast<std::size_t>(selTo - selFrom)),
                       t.accentText, t.accentBg);
        if (selTo < n)
            screen.putText(bounds_.x + gut + selTo, bounds_.y + row,
                           cell.substr(static_cast<std::size_t>(selTo)),
                           t.editorText, t.editorBg);
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
