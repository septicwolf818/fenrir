#include "fenrir/widgets/FileManagerView.hpp"

namespace fenrir {

FileManagerView::FileManagerView(AppContext& ctx, std::string startDir)
    : ctx_(ctx), listing_(std::move(startDir)) {}

void FileManagerView::status(const std::string& msg) {
    if (onStatus) onStatus(msg);
}

void FileManagerView::draw(Screen& screen) {
    Rect listArea{bounds_.x, bounds_.y, bounds_.w, std::max(0, bounds_.h - 1)};
    drawDirectoryListing(screen, listArea, listing_, focused_);

    int hintY = bounds_.y + bounds_.h - 1;
    std::u32string hint = U"Enter otworz | Backspace w gore | Ctrl+G idz do | Ctrl+C kopiuj jako";
    for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x) screen.set(x, hintY, Cell{U' ', colors::Black, colors::Gray, 0});
    if (static_cast<int>(hint.size()) > bounds_.w) hint.resize(static_cast<std::size_t>(bounds_.w));
    screen.putText(bounds_.x, hintY, hint, colors::Black, colors::Gray);
}

bool FileManagerView::handleKey(const KeyEvent& ev) {
    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'g' || ev.character == U'G')) {
        ctx_.openFolder(listing_.currentDir(), [this](const std::string& picked) {
            listing_ = DirectoryListing(picked);
            status("Przeszedles do: " + picked);
        });
        return true;
    }

    if (ev.ctrl && ev.key == Key::Char && (ev.character == U'c' || ev.character == U'C')) {
        if (!listing_.selectedIsFile()) {
            status("Zaznacz plik (nie katalog), zeby go skopiowac.");
            return true;
        }
        std::string srcPath = listing_.selectedFullPath();
        std::string srcName = listing_.entries()[static_cast<std::size_t>(listing_.selectedIndex())].name;
        // Domyslna nazwa kopii NIE moze byc identyczna jak zrodlo w tym samym
        // katalogu - to realnie niszczylo plik (patrz fs::copyFile). Wstawiamy
        // "-kopia" przed rozszerzeniem jako bezpieczna podpowiedz; i tak mozna
        // to nadpisac w polu nazwy albo zmienic katalog w dialogu.
        std::string suggested;
        auto dot = srcName.find_last_of('.');
        if (dot != std::string::npos && dot != 0) {
            suggested = srcName.substr(0, dot) + "-kopia" + srcName.substr(dot);
        } else {
            suggested = srcName + "-kopia";
        }
        ctx_.saveFile(listing_.currentDir(), suggested, [this, srcPath](const std::string& destPath) {
            try {
                fs::copyFile(srcPath, destPath);
                listing_.reload();
                status("Skopiowano do: " + destPath);
            } catch (const std::exception& e) {
                status(std::string("Blad kopiowania: ") + e.what());
            }
        });
        return true;
    }

    switch (ev.key) {
        case Key::Up: listing_.moveSelection(-1); return true;
        case Key::Down: listing_.moveSelection(1); return true;
        case Key::Backspace: listing_.goUp(); return true;
        case Key::Enter: {
            if (!listing_.activateSelected()) {
                long long sz = fs::fileSize(listing_.selectedFullPath());
                status("Plik: " + listing_.selectedFullPath() +
                       (sz >= 0 ? " (" + std::to_string(sz) + " B)" : ""));
            }
            return true;
        }
        default: return false;
    }
}

} // namespace fenrir
