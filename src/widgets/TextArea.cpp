#include "fenrir/widgets/TextArea.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"

namespace fenrir {

TextArea::TextArea() { editable_ = true; }

// --- Layout (linie logiczne -> wiersze wizualne) -----------------------------

void TextArea::rebuildLayout() const {
    layoutW_ = std::max(1, bounds_.w);
    layout_.clear();

    for (int li = 0; li < static_cast<int>(lines_.size()); ++li) {
        const std::u32string& line = lines_[static_cast<std::size_t>(li)];
        if (line.empty()) {
            layout_.push_back(Seg{li, 0, 0}); // pusta linia = jeden pusty wiersz
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
            // Lam na ostatniej spacji w oknie; spacja zostaje na koncu wiersza
            // (dzieki temu segmenty sa ciagle i kazdy znak wystepuje raz).
            int brk = -1;
            for (int k = pos + layoutW_ - 1; k > pos; --k) {
                if (line[static_cast<std::size_t>(k)] == U' ') { brk = k; break; }
            }
            const int len = (brk > pos) ? (brk - pos + 1) : layoutW_; // brak spacji = twardy lam
            layout_.push_back(Seg{li, pos, len});
            pos += len;
        }
    }
    if (layout_.empty()) layout_.push_back(Seg{0, 0, 0});
    layoutDirty_ = false;
}

void TextArea::ensureLayout() const {
    if (!layoutDirty_ && layoutW_ == std::max(1, bounds_.w)) return;
    rebuildLayout();
}

int TextArea::visualRowOf(int row, int col) const {
    // Zwraca index wiersza wizualnego dla (row, col). Kolumna na koncu linii
    // (i dalej) trafia do OSTATNIEGO wiersza wizualnego tej linii.
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

// --- Edycja ------------------------------------------------------------------

void TextArea::loadFile(const std::string& path) {
    path_ = path;
    std::ifstream f(path, std::ios::binary);
    lines_.clear();
    if (!f) {
        lines_.push_back(std::u32string());
        dirty_ = false;
        markDirty();
        if (onStatus) onStatus("Nowy plik: " + path);
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
    goalCol_ = 0;
    dirty_ = false;
    markDirty();
    if (onStatus) onStatus("Wczytano: " + path + " (" + std::to_string(lines_.size()) + " linii)");
}

void TextArea::saveFile(const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (onStatus) onStatus("Blad zapisu: " + path);
        return;
    }
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        f << text::toUtf8(lines_[i]);
        if (i + 1 < lines_.size()) f << '\n';
    }
    path_ = path;
    dirty_ = false;
    if (onStatus) onStatus("Zapisano: " + path);
}

void TextArea::saveToCurrentPath() {
    if (path_.empty()) {
        if (onStatus) onStatus("Brak nazwy pliku - uzyj saveFile(sciezka)");
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
    goalCol_ = 0;
    path_.clear();
    dirty_ = false;
    markDirty();
    if (onStatus) onStatus("Nowy plik");
}

void TextArea::clearText() {
    lines_.clear();
    lines_.push_back(std::u32string());
    cursorRow_ = 0;
    cursorCol_ = 0;
    scrollRow_ = 0;
    goalCol_ = 0;
    dirty_ = true;
    markDirty();
    if (onStatus) onStatus("Wyczyszczono");
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

// --- Ruch kursorem -----------------------------------------------------------

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
    auto [row, col] = logicalOf(target, goalCol_); // lepki cel: ten sam offset wizualny
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
}

// --- Klawiatura --------------------------------------------------------------

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
    switch (ev.key) {
        case Key::Up:    moveVert(-1); return KeyEventResult::Consumed;
        case Key::Down:  moveVert(+1); return KeyEventResult::Consumed;
        case Key::Left:  moveHoriz(-1); return KeyEventResult::Consumed;
        case Key::Right: moveHoriz(+1); return KeyEventResult::Consumed;
        case Key::Home: { // Home = poczatek biezacego wiersza wizualnego
            ensureLayout();
            cursorCol_ = layout_[static_cast<std::size_t>(visualRowOf(cursorRow_, cursorCol_))].col0;
            syncGoal();
            ensureVisible();
            return KeyEventResult::Consumed;
        }
        case Key::End: { // End = koniec biezacego wiersza wizualnego
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
        case Key::Char:
            if (!ev.ctrl) { insertChar(ev.character); ensureVisible(); return KeyEventResult::Consumed; }
            return KeyEventResult::Ignored;
        default: return KeyEventResult::Ignored;
    }
}

// --- Rysowanie ---------------------------------------------------------------

void TextArea::draw(Screen& screen) {
    ensureLayout();
    const auto& t = theme::active;
    const Cell fill{U' ', t.editorText, t.editorBg, 0};

    for (int row = 0; row < bounds_.h; ++row) {
        screen.fillRect(Rect{bounds_.x, bounds_.y + row, bounds_.w, 1}, fill);
        const int segIdx = scrollRow_ + row;
        if (segIdx >= static_cast<int>(layout_.size())) continue; // za tekstem - samo tlo
        const Seg& s = layout_[static_cast<std::size_t>(segIdx)];
        const std::u32string& line = lines_[static_cast<std::size_t>(s.line)];
        if (s.len <= 0) continue;
        const int n = std::min(s.len, bounds_.w);
        screen.putText(bounds_.x, bounds_.y + row,
                       std::u32string_view(line).substr(static_cast<std::size_t>(s.col0),
                                                        static_cast<std::size_t>(n)),
                       t.editorText, t.editorBg);
    }
}

std::optional<std::pair<int, int>> TextArea::cursorPosition() const {
    ensureLayout();
    const int seg = visualRowOf(cursorRow_, cursorCol_);
    const int screenRow = seg - scrollRow_;
    if (screenRow < 0 || screenRow >= bounds_.h || bounds_.w <= 0) return std::nullopt;
    int offset = visualOffset();
    if (offset >= bounds_.w) offset = bounds_.w - 1; // koniec pelnego wiersza wizualnego
    return std::make_pair(bounds_.x + offset, bounds_.y + screenRow);
}

} // namespace fenrir
