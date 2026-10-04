#include "fenrir/widgets/TextArea.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace fenrir {

namespace {

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::u32string decodeUtf8Line(const std::string& line) {
    std::u32string out;
    std::size_t i = 0;
    while (i < line.size()) {
        unsigned char b0 = static_cast<unsigned char>(line[i]);
        auto cont = [&](std::size_t idx) -> int {
            if (idx >= line.size()) return -1;
            unsigned char b = static_cast<unsigned char>(line[idx]);
            if ((b & 0xC0) != 0x80) return -1;
            return b & 0x3F;
        };
        if (b0 < 0x80) { out += static_cast<char32_t>(b0); i += 1; }
        else if ((b0 & 0xE0) == 0xC0) {
            int c1 = cont(i + 1);
            if (c1 < 0) { out += static_cast<char32_t>(b0); i += 1; continue; }
            out += static_cast<char32_t>(((b0 & 0x1F) << 6) | c1);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            int c1 = cont(i + 1), c2 = cont(i + 2);
            if (c1 < 0 || c2 < 0) { out += static_cast<char32_t>(b0); i += 1; continue; }
            out += static_cast<char32_t>(((b0 & 0x0F) << 12) | (c1 << 6) | c2);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            int c1 = cont(i + 1), c2 = cont(i + 2), c3 = cont(i + 3);
            if (c1 < 0 || c2 < 0 || c3 < 0) { out += static_cast<char32_t>(b0); i += 1; continue; }
            out += static_cast<char32_t>(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3);
            i += 4;
        } else { out += static_cast<char32_t>(b0); i += 1; }
    }
    return out;
}

} // namespace

TextArea::TextArea() = default;

void TextArea::loadFile(const std::string& path) {
    path_ = path;
    std::ifstream f(path, std::ios::binary);
    lines_.clear();
    if (!f) {
        lines_.push_back(std::u32string());
        dirty_ = false;
        if (onStatus) onStatus("Nowy plik: " + path);
        return;
    }
    std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::string current;
    for (char c : raw) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            lines_.push_back(decodeUtf8Line(current));
            current.clear();
        } else {
            current += c;
        }
    }
    lines_.push_back(decodeUtf8Line(current));
    if (lines_.empty()) lines_.push_back(std::u32string());

    cursorRow_ = 0;
    cursorCol_ = 0;
    scrollRow_ = 0;
    dirty_ = false;
    if (onStatus) onStatus("Wczytano: " + path + " (" + std::to_string(lines_.size()) + " linii)");
}

void TextArea::saveFile(const std::string& path) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        if (onStatus) onStatus("Blad zapisu: " + path);
        return;
    }
    for (std::size_t i = 0; i < lines_.size(); ++i) {
        std::string encoded;
        for (char32_t ch : lines_[i]) appendUtf8(encoded, ch);
        f << encoded;
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

void TextArea::insertChar(char32_t ch) {
    lines_[cursorRow_].insert(lines_[cursorRow_].begin() + cursorCol_, ch);
    ++cursorCol_;
    dirty_ = true;
}

void TextArea::insertNewline() {
    std::u32string rest = lines_[cursorRow_].substr(cursorCol_);
    lines_[cursorRow_].resize(cursorCol_);
    lines_.insert(lines_.begin() + cursorRow_ + 1, rest);
    ++cursorRow_;
    cursorCol_ = 0;
    dirty_ = true;
}

void TextArea::backspace() {
    if (cursorCol_ > 0) {
        lines_[cursorRow_].erase(lines_[cursorRow_].begin() + cursorCol_ - 1);
        --cursorCol_;
        dirty_ = true;
    } else if (cursorRow_ > 0) {
        int prevLen = static_cast<int>(lines_[cursorRow_ - 1].size());
        lines_[cursorRow_ - 1] += lines_[cursorRow_];
        lines_.erase(lines_.begin() + cursorRow_);
        --cursorRow_;
        cursorCol_ = prevLen;
        dirty_ = true;
    }
}

void TextArea::del() {
    if (cursorCol_ < static_cast<int>(lines_[cursorRow_].size())) {
        lines_[cursorRow_].erase(lines_[cursorRow_].begin() + cursorCol_);
        dirty_ = true;
    } else if (cursorRow_ + 1 < static_cast<int>(lines_.size())) {
        lines_[cursorRow_] += lines_[cursorRow_ + 1];
        lines_.erase(lines_.begin() + cursorRow_ + 1);
        dirty_ = true;
    }
}

void TextArea::clampCursor() {
    cursorRow_ = std::clamp(cursorRow_, 0, static_cast<int>(lines_.size()) - 1);
    cursorCol_ = std::clamp(cursorCol_, 0, static_cast<int>(lines_[cursorRow_].size()));
}

void TextArea::moveCursor(int dRow, int dCol) {
    cursorRow_ += dRow;
    clampCursor();
    if (dCol != 0) {
        cursorCol_ += dCol;
        if (cursorCol_ < 0) {
            if (cursorRow_ > 0) { --cursorRow_; cursorCol_ = static_cast<int>(lines_[cursorRow_].size()); }
            else cursorCol_ = 0;
        } else if (cursorCol_ > static_cast<int>(lines_[cursorRow_].size())) {
            if (cursorRow_ + 1 < static_cast<int>(lines_.size())) { ++cursorRow_; cursorCol_ = 0; }
            else cursorCol_ = static_cast<int>(lines_[cursorRow_].size());
        }
    }
    clampCursor();
    ensureVisible();
}

void TextArea::ensureVisible() {
    const int visibleRows = std::max(1, bounds_.h);
    if (cursorRow_ < scrollRow_) scrollRow_ = cursorRow_;
    if (cursorRow_ >= scrollRow_ + visibleRows) scrollRow_ = cursorRow_ - visibleRows + 1;
}

bool TextArea::handleKey(const KeyEvent& ev) {
    if (ev.ctrl && ev.key == Key::Char && (ev.character == U's' || ev.character == U'S')) {
        if (onSaveRequested) onSaveRequested();
        else saveToCurrentPath();
        return true;
    }
    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'o' || ev.character == U'O')) {
        if (onOpenRequested) onOpenRequested();
        return true;
    }
    switch (ev.key) {
        case Key::Up: moveCursor(-1, 0); return true;
        case Key::Down: moveCursor(1, 0); return true;
        case Key::Left: moveCursor(0, -1); return true;
        case Key::Right: moveCursor(0, 1); return true;
        case Key::Home: cursorCol_ = 0; ensureVisible(); return true;
        case Key::End: cursorCol_ = static_cast<int>(lines_[cursorRow_].size()); ensureVisible(); return true;
        case Key::PageUp: moveCursor(-std::max(1, bounds_.h), 0); return true;
        case Key::PageDown: moveCursor(std::max(1, bounds_.h), 0); return true;
        case Key::Enter: insertNewline(); ensureVisible(); return true;
        case Key::Backspace: backspace(); ensureVisible(); return true;
        case Key::Delete: del(); return true;
        case Key::Char:
            if (!ev.ctrl) { insertChar(ev.character); ensureVisible(); return true; }
            return false;
        default: return false;
    }
}

void TextArea::draw(Screen& screen) {
    const Color fg = colors::White;
    const Color bg = colors::Black;

    for (int row = 0; row < bounds_.h; ++row) {
        int lineIdx = scrollRow_ + row;
        for (int col = 0; col < bounds_.w; ++col) {
            char32_t ch = U' ';
            if (lineIdx < static_cast<int>(lines_.size()) && col < static_cast<int>(lines_[lineIdx].size()))
                ch = lines_[lineIdx][col];
            screen.set(bounds_.x + col, bounds_.y + row, Cell{ch, fg, bg, 0});
        }
    }
}

std::optional<std::pair<int, int>> TextArea::cursorPosition() const {
    int screenRow = cursorRow_ - scrollRow_;
    if (screenRow < 0 || screenRow >= bounds_.h) return std::nullopt;
    int screenCol = cursorCol_;
    if (screenCol >= bounds_.w) return std::nullopt;
    return std::make_pair(bounds_.x + screenCol, bounds_.y + screenRow);
}

} // namespace fenrir
