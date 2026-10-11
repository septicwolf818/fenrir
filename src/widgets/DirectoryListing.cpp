#include "fenrir/widgets/DirectoryListing.hpp"

#include <algorithm>

namespace fenrir {

DirectoryListing::DirectoryListing(std::string startDir) { loadDir(std::move(startDir)); }

void DirectoryListing::loadDir(const std::string& dir) {
    dir_ = dir;
    entries_.clear();
    error_.clear();
    selected_ = 0;
    scroll_ = 0;

    std::string par = fs::parent(dir_);
    if (par != dir_) entries_.push_back(fs::Entry{"..", true});

    try {
        auto listed = fs::list(dir_);
        entries_.insert(entries_.end(), listed.begin(), listed.end());
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void DirectoryListing::reload() { loadDir(dir_); }

void DirectoryListing::goUp() { loadDir(fs::parent(dir_)); }

void DirectoryListing::moveSelection(int delta) {
    if (entries_.empty()) return;
    int n = static_cast<int>(entries_.size());
    selected_ = std::clamp(selected_ + delta, 0, n - 1);
}

bool DirectoryListing::activateSelected() {
    if (entries_.empty()) return false;
    const fs::Entry& e = entries_[static_cast<std::size_t>(selected_)];
    if (!e.isDir) return false;

    if (e.name == "..") loadDir(fs::parent(dir_));
    else loadDir(fs::join(dir_, e.name));
    return true;
}

bool DirectoryListing::selectedIsFile() const {
    if (entries_.empty()) return false;
    return !entries_[static_cast<std::size_t>(selected_)].isDir;
}

std::string DirectoryListing::selectedFullPath() const {
    if (entries_.empty()) return dir_;
    const fs::Entry& e = entries_[static_cast<std::size_t>(selected_)];
    if (e.name == "..") return fs::parent(dir_);
    return fs::join(dir_, e.name);
}

void DirectoryListing::ensureVisible(int viewportHeight) {
    if (viewportHeight <= 0) return;
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + viewportHeight) scroll_ = selected_ - viewportHeight + 1;
    if (scroll_ < 0) scroll_ = 0;
}

void drawDirectoryListing(Screen& screen, const Rect& area, DirectoryListing& listing) {
    if (area.h <= 0 || area.w <= 0) return;

    // Naglowek (etykieta katalogu): Gray tlo, White tekst, bold — drugorzędowy pasek.
    std::string dirLine = listing.currentDir();
    std::u32string header(dirLine.begin(), dirLine.end());
    if (static_cast<int>(header.size()) > area.w) header.resize(static_cast<std::size_t>(area.w));
    for (int x = area.x; x < area.x + area.w; ++x)
        screen.set(x, area.y, Cell{U' ', colors::White, colors::Gray, StyleBold});
    screen.putText(area.x, area.y, header, colors::White, colors::Gray, StyleBold);

    // Tlo listy: White (papier). Wiersze: Black tekst na White tle.
    const Rect listArea{area.x, area.y + 1, area.w, area.h - 1};
    for (int y = listArea.y; y < listArea.y + listArea.h; ++y)
        for (int x = listArea.x; x < listArea.x + listArea.w; ++x)
            screen.set(x, y, Cell{U' ', colors::Black, colors::White, 0});

    if (listing.hasError()) {
        std::string err = listing.error();
        std::u32string e(err.begin(), err.end());
        if (static_cast<int>(e.size()) > listArea.w) e.resize(static_cast<std::size_t>(listArea.w));
        screen.putText(listArea.x, listArea.y, e, colors::BrightRed, colors::White, StyleBold);
        return;
    }

    listing.ensureVisible(listArea.h);
    const auto& entries = listing.entries();
    for (int row = 0; row < listArea.h; ++row) {
        int idx = listing.scrollOffset() + row;
        if (idx < 0 || idx >= static_cast<int>(entries.size())) break;
        const fs::Entry& e = entries[static_cast<std::size_t>(idx)];

        // Wybrany wiersz: White tekst na Blue tle, bold — ten sam motyw co pasek tytulu.
        bool isSel = (idx == listing.selectedIndex());
        Color fg = isSel ? colors::White : colors::Black;
        Color bg = isSel ? colors::Blue : colors::White;
        uint8_t style = isSel ? StyleBold : StyleNone;

        std::string label = (e.isDir ? "/ " : "  ") + e.name;
        std::u32string line(label.begin(), label.end());
        if (static_cast<int>(line.size()) > listArea.w) line.resize(static_cast<std::size_t>(listArea.w));

        for (int x = listArea.x; x < listArea.x + listArea.w; ++x)
            screen.set(x, listArea.y + row, Cell{U' ', fg, bg, style});
        screen.putText(listArea.x, listArea.y + row, line, fg, bg, style);
    }
}

} // namespace fenrir
