#include "fenrir/widgets/FileManagerView.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace stdfs = std::filesystem;

namespace fenrir {

// Win95 palette for file manager:
//   Panel header: Gray bg, White fg  (drugorzędowy)
//   File list:    White bg, Black fg (Win95 "papier")
//   Selected row: Black on White, bold
//   Hint bar:     Gray bg, White fg

FileManagerView::FileManagerView(AppContext& ctx, std::string startDir)
    : ctx_(ctx), left_(startDir), right_(".") {}

void FileManagerView::status(const std::string& msg) {
    if (onStatus) onStatus(msg);
}

static std::string extractFilename(const std::string& path) {
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

void FileManagerView::drawPanel(Screen& screen, const Rect& area, DirectoryListing& listing) {
    if (area.h <= 0 || area.w <= 0) return;

    // Naglowek (etykieta panelu): Gray bg, White fg — drugorzędowy,
    // nie niebieski (niebieski = pasek tytułu okna).
    std::u32string header = U" " + std::u32string(listing.currentDir().begin(), listing.currentDir().end());
    if (static_cast<int>(header.size()) > area.w) header.resize(static_cast<std::size_t>(area.w));
    for (int x = area.x; x < area.x + area.w; ++x)
        screen.set(x, area.y, Cell{U' ', colors::White, colors::Gray, StyleBold});
    screen.putText(area.x, area.y, header, colors::White, colors::Gray, StyleBold);

    // Tlo listy plikow: White (papier). Wiersze: Black tekst na White tle.
    const Rect listArea{area.x, area.y + 1, area.w, area.h};
    for (int y = listArea.y; y < listArea.y + listArea.h; ++y)
        for (int x = listArea.x; x < listArea.x + listArea.w; ++x)
            screen.set(x, y, Cell{U' ', colors::Black, colors::White, 0});

    if (listing.hasError()) {
        std::u32string err(listing.error().begin(), listing.error().end());
        if (static_cast<int>(err.size()) > listArea.w) err.resize(static_cast<std::size_t>(listArea.w));
        screen.putText(listArea.x, listArea.y, err, colors::BrightRed, colors::White, StyleBold);
        return;
    }

    listing.ensureVisible(listArea.h);
    const auto& entries = listing.entries();
    for (int row = 0; row < listArea.h; ++row) {
        int idx = listing.scrollOffset() + row;
        if (idx < 0 || idx >= static_cast<int>(entries.size())) break;
        const fs::Entry& e = entries[static_cast<std::size_t>(idx)];

        // Wybrany wiersz: White tekst na Blue tle, bold — spójnie z paskiem tytułu.
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

void FileManagerView::draw(Screen& screen) {
    const int spacer = 1;
    const int panelW = (bounds_.w - spacer) / 2;

    Rect leftArea{bounds_.x, bounds_.y, panelW, std::max(0, bounds_.h - 1)};
    Rect rightArea{bounds_.x + panelW + spacer, bounds_.y, panelW, std::max(0, bounds_.h - 1)};

    drawPanel(screen, leftArea, left_);
    drawPanel(screen, rightArea, right_);

    // Pasek hint na dole: Gray bg, White fg (czytelniej niz czarny na szarym).
    int hintY = bounds_.y + bounds_.h - 1;
    std::u32string hint = U"Tab panel | Enter katalog | Backspace gore";
    for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x)
        screen.set(x, hintY, Cell{U' ', colors::White, colors::Gray, 0});
    if (static_cast<int>(hint.size()) > bounds_.w) hint.resize(static_cast<std::size_t>(bounds_.w));
    screen.putText(bounds_.x, hintY, hint, colors::White, colors::Gray);
}

// --- Operacje na plikach ----------------------------------------------------

bool FileManagerView::copyClipboardTo(DirectoryListing& dest, std::string& errMsg) {
    if (clipboard_.empty()) { errMsg = "Schowek pusty"; return false; }
    int ok = 0, errCount = 0;
    for (auto& src : clipboard_) {
        try {
            std::string base = extractFilename(src);
            std::string dst = fs::join(dest.currentDir(), base);
            int n = 0;
            while (fs::exists(dst)) {
                auto dot = base.find_last_of('.');
                std::string copy;
                if (dot != std::string::npos && dot != base.size() - 1)
                    copy = base.substr(0, dot) + " (" + std::to_string(++n) + ")" + base.substr(dot);
                else
                    copy = base + " (" + std::to_string(++n) + ")";
                dst = fs::join(dest.currentDir(), copy);
            }
            fs::copyFile(src, dst);
            ok++;
        } catch (const std::exception& e) { errMsg += e.what(); errMsg += ';'; errCount++; }
    }
    dest.reload();
    errMsg = "Skopiowano: " + std::to_string(ok) + ", bledy: " + std::to_string(errCount);
    return errCount == 0;
}

bool FileManagerView::moveClipboardTo(DirectoryListing& dest, std::string& errMsg) {
    if (clipboard_.empty()) { errMsg = "Schowek pusty"; return false; }
    int ok = 0, errCount = 0;
    for (auto& src : clipboard_) {
        try {
            std::string base = extractFilename(src);
            std::string dst = fs::join(dest.currentDir(), base);
            if (!fs::exists(dst)) {
                fs::copyFile(src, dst);
            } else {
                int n = 0;
                std::string origSrc = src;
                while (fs::exists(dst)) {
                    auto dot = base.find_last_of('.');
                    std::string copy;
                    if (dot != std::string::npos && dot != base.size() - 1)
                        copy = base.substr(0, dot) + " (" + std::to_string(++n) + ")" + base.substr(dot);
                    else
                        copy = base + " (" + std::to_string(++n) + ")";
                    dst = fs::join(dest.currentDir(), copy);
                }
                fs::copyFile(origSrc, dst);
            }
            { std::error_code ec; stdfs::remove(src, ec); }
            ok++;
        } catch (const std::exception& e) { errMsg += e.what(); errMsg += ';'; errCount++; }
    }
    dest.reload();
    if (errCount == 0) {
        clipboard_.clear();
        clipOp_ = ClipboardOp::Copy;
    }
    errMsg = "Przeniesiono: " + std::to_string(ok) + ", bledy: " + std::to_string(errCount);
    return errCount == 0;
}

bool FileManagerView::deleteClipboardItems(std::string& errMsg) {
    if (clipboard_.empty()) { errMsg = "Nic do usuniecia"; return false; }
    int ok = 0, errCount = 0;
    for (auto& p : clipboard_) {
        try {
            bool isDir = fs::isDirectory(p);
            std::error_code ec;
            if (isDir) {
                stdfs::remove_all(p, ec);
            } else {
                stdfs::remove(p, ec);
            }
            ok++;
        } catch (const std::exception& e) { errMsg += e.what(); errMsg += ';'; errCount++; }
    }
    left_.reload(); right_.reload();
    if (ok > 0 && errCount == 0) clipboard_.clear();
    errMsg = "Usunieto: " + std::to_string(ok) + ", bledy: " + std::to_string(errCount);
    return errCount == 0;
}

void FileManagerView::goUp() {
    DirectoryListing& active = rightActive_ ? right_ : left_;
    active.goUp();
    status("W gorze: " + active.currentDir());
}

void FileManagerView::pasteToOtherPane() {
    if (clipboard_.empty()) { status("Schowek pusty"); return; }
    DirectoryListing* dest = rightActive_ ? &left_ : &right_;
    std::string errMsg;
    if (clipOp_ == ClipboardOp::Copy) copyClipboardTo(*dest, errMsg);
    else moveClipboardTo(*dest, errMsg);
    status(errMsg);
}

void FileManagerView::renameSelected() {
    DirectoryListing& active = rightActive_ ? right_ : left_;
    if (active.entries().empty()) { status("Brak elementow."); return; }
    std::string oldPath = active.selectedFullPath();
    std::string baseName = extractFilename(oldPath);
    ctx_.saveFile(active.currentDir(), baseName,
        [this, oldPath](const std::string& newPath) {
            try {
                fs::copyFile(oldPath, newPath);
                bool isDir = fs::isDirectory(oldPath);
                if (isDir) stdfs::remove_all(oldPath);
                else stdfs::remove(oldPath);
                left_.reload(); right_.reload();
                status("Przemianowano.");
            } catch (const std::exception& e) {
                status(std::string("Błąd: ") + e.what());
            }
        }, [](){});
}

void FileManagerView::deleteSelected() {
    DirectoryListing& active = rightActive_ ? right_ : left_;
    if (active.entries().empty()) { status("Brak elementow."); return; }
    std::string errMsg;
    clipboard_.clear();
    clipboard_.push_back(active.selectedFullPath());
    deleteClipboardItems(errMsg);
    status(errMsg);
}

void FileManagerView::refreshBoth() {
    left_.reload(); right_.reload();
    status("Odswiezono oba panele.");
}

void FileManagerView::goToFolder() {
    DirectoryListing& active = rightActive_ ? right_ : left_;
    ctx_.openFolder(active.currentDir(), [this](const std::string& picked) {
        if (rightActive_) {
            right_ = DirectoryListing(picked);
        } else {
            left_ = DirectoryListing(picked);
        }
        status("Panel: " + picked);
    });
}

KeyEventResult FileManagerView::handleKey(const KeyEvent& ev) {
    if (ev.key == Key::Tab) { rightActive_ = !rightActive_; return KeyEventResult::Consumed; }

    if (ev.key == Key::Left && clipboard_.empty()) {
        if (!rightActive_) { } else { rightActive_ = false; }
        return KeyEventResult::Consumed;
    }
    if (ev.key == Key::Right && clipboard_.empty()) {
        if (rightActive_) { } else { rightActive_ = true; }
        return KeyEventResult::Consumed;
    }

    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'g' || ev.character == U'G')) {
        DirectoryListing& active = rightActive_ ? right_ : left_;
        ctx_.openFolder(active.currentDir(), [this, &active](const std::string& picked) {
            active.reload();
            status("Panel " + std::string(rightActive_ ? "Prawy" : "Lewy") + ": " + picked);
        });
        return KeyEventResult::Consumed;
    }

    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'c' || ev.character == U'C')) {
        DirectoryListing& active = rightActive_ ? right_ : left_;
        clipboard_.clear();
        clipboard_.push_back(active.selectedFullPath());
        clipOp_ = ClipboardOp::Copy;
        status("Skopiowano: " + extractFilename(active.selectedFullPath()));
        return KeyEventResult::Consumed;
    }

    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'x' || ev.character == U'X')) {
        DirectoryListing& active = rightActive_ ? right_ : left_;
        clipboard_.clear();
        clipboard_.push_back(active.selectedFullPath());
        clipOp_ = ClipboardOp::Cut;
        status("Przeniesienie: " + extractFilename(active.selectedFullPath()));
        return KeyEventResult::Consumed;
    }

    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'v' || ev.character == U'V')) {
        DirectoryListing* dest = rightActive_ ? &left_ : &right_;
        std::string errMsg;
        if (clipOp_ == ClipboardOp::Copy) copyClipboardTo(*dest, errMsg);
        else moveClipboardTo(*dest, errMsg);
        status(errMsg);
        return KeyEventResult::Consumed;
    }

    if (ev.key == Key::Delete) {
        DirectoryListing& active = rightActive_ ? right_ : left_;
        std::string errMsg;
        clipboard_.clear();
        clipboard_.push_back(active.selectedFullPath());
        deleteClipboardItems(errMsg);
        status(errMsg);
        return KeyEventResult::Consumed;
    }

    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'r' || ev.character == U'R')) {
        DirectoryListing& active = rightActive_ ? right_ : left_;
        if (active.entries().empty()) { status("Brak elementow."); return KeyEventResult::Consumed; }
        std::string oldPath = active.selectedFullPath();
        std::string baseName = extractFilename(oldPath);
        ctx_.saveFile(active.currentDir(), baseName,
            [this, oldPath](const std::string& newPath) {
                try {
                    fs::copyFile(oldPath, newPath);
                    bool isDir = fs::isDirectory(oldPath);
                    if (isDir) stdfs::remove_all(oldPath);
                    else stdfs::remove(oldPath);
                    left_.reload(); right_.reload();
                    status("Przemianowano.");
                } catch (const std::exception& e) {
                    status(std::string("Błąd: ") + e.what());
                }
            }, [](){});
        return KeyEventResult::Consumed;
    }

    switch (ev.key) {
        case Key::Up: rightActive_ ? right_.moveSelection(-1) : left_.moveSelection(-1); break;
        case Key::Down: rightActive_ ? right_.moveSelection(1) : left_.moveSelection(1); break;
        case Key::Backspace: {
            DirectoryListing& active = rightActive_ ? right_ : left_;
            active.goUp();
            status("W gorze: " + active.currentDir());
            return KeyEventResult::Consumed;
        }
        case Key::Enter: {
            DirectoryListing& active = rightActive_ ? right_ : left_;
            if (!active.activateSelected()) {
                long long sz = fs::fileSize(active.selectedFullPath());
                status("Plik: " + active.selectedFullPath() +
                       (sz >= 0 ? " (" + std::to_string(sz) + " B)" : ""));
                return KeyEventResult::Activated;
            }
            status("Wszedles do: " + active.currentDir());
            return KeyEventResult::Activated;
        }
        default: return KeyEventResult::Ignored;
    }
    return KeyEventResult::Consumed;
}

} // namespace fenrir
