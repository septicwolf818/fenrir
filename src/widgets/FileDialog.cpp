#include "fenrir/widgets/FileDialog.hpp"

#include <algorithm>

namespace fenrir {

FileDialog::FileDialog(FileDialogMode mode, std::string startDir, std::string suggestedName)
    : mode_(mode), listing_(std::move(startDir)) {
    filename_.assign(suggestedName.begin(), suggestedName.end());
}

Rect FileDialog::listingArea() const {
    int h = std::max(0, bounds_.h - kHintRows - nameFieldRows());
    return Rect{bounds_.x, bounds_.y, bounds_.w, h};
}

void FileDialog::confirmSave() {
    if (filename_.empty()) return;
    std::string name(filename_.begin(), filename_.end());
    std::string path = fs::join(listing_.currentDir(), name);
    if (onConfirm) onConfirm(path);
}

void FileDialog::draw(Screen& screen) {
    Rect la = listingArea();
    drawDirectoryListing(screen, la, listing_);

    if (mode_ == FileDialogMode::SaveFile) {
        int y = la.y + la.h;
        // Pole nazwy: Black tekst na White tle (papier). Gdy zaznaczone (fokus): White na Blue.
        bool sel = (zone_ == Zone::NameField);
        Color fg = sel ? colors::White : colors::Black;
        Color bg = sel ? colors::Blue : colors::White;
        std::u32string label = U"Nazwa: " + filename_;
        for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x) screen.set(x, y, Cell{U' ', fg, bg, 0});
        if (static_cast<int>(label.size()) > bounds_.w) label.resize(static_cast<std::size_t>(bounds_.w));
        screen.putText(bounds_.x, y, label, fg, bg, sel ? StyleBold : StyleNone);
    }

    int hintY = bounds_.y + bounds_.h - kHintRows;
    std::u32string hint;
    switch (mode_) {
        case FileDialogMode::OpenFile: hint = U"Enter: wejdz/wybierz plik | Esc: anuluj"; break;
        case FileDialogMode::OpenFolder: hint = U"Enter: wejdz | S: wybierz ten folder | Esc: anuluj"; break;
        case FileDialogMode::SaveFile: hint = U"Enter: wejdz/wpisz nazwe | Tab: pole nazwy | Esc: anuluj"; break;
    }
    for (int x = bounds_.x; x < bounds_.x + bounds_.w; ++x) screen.set(x, hintY, Cell{U' ', colors::White, colors::Gray, 0});
    if (static_cast<int>(hint.size()) > bounds_.w) hint.resize(static_cast<std::size_t>(bounds_.w));
    screen.putText(bounds_.x, hintY, hint, colors::White, colors::Gray);
}

KeyEventResult FileDialog::handleKey(const KeyEvent& ev) {
    if (ev.key == Key::Escape) { if (onCancel) onCancel(); return KeyEventResult::Consumed; }

    if (mode_ == FileDialogMode::SaveFile && ev.key == Key::Tab) {
        zone_ = (zone_ == Zone::List) ? Zone::NameField : Zone::List;
        return KeyEventResult::Consumed;
    }

    if (zone_ == Zone::NameField) {
        if (ev.key == Key::Backspace) { if (!filename_.empty()) filename_.pop_back(); return KeyEventResult::Consumed; }
        if (ev.key == Key::Enter) { confirmSave(); return KeyEventResult::Activated; }
        if (ev.key == Key::Char && !ev.ctrl) { filename_ += ev.character; return KeyEventResult::Consumed; }
        return KeyEventResult::Consumed;
    }

    if (mode_ == FileDialogMode::OpenFolder && ev.key == Key::Char && !ev.ctrl &&
        (ev.character == U's' || ev.character == U'S')) {
        if (onConfirm) onConfirm(listing_.currentDir());
        return KeyEventResult::Activated;
    }

    switch (ev.key) {
        case Key::Up: listing_.moveSelection(-1); return KeyEventResult::Consumed;
        case Key::Down: listing_.moveSelection(1); return KeyEventResult::Consumed;
        case Key::Enter: {
            if (listing_.activateSelected()) return KeyEventResult::Consumed;
            if (mode_ == FileDialogMode::OpenFile) {
                if (onConfirm) onConfirm(listing_.selectedFullPath());
                return KeyEventResult::Activated;
            } else if (mode_ == FileDialogMode::SaveFile) {
                std::string name = listing_.entries()[static_cast<std::size_t>(listing_.selectedIndex())].name;
                filename_.assign(name.begin(), name.end());
                zone_ = Zone::NameField;
            }
            return KeyEventResult::Activated;
        }
        default: return KeyEventResult::Ignored;
    }
}

std::optional<std::pair<int, int>> FileDialog::cursorPosition() const {
    if (zone_ != Zone::NameField) return std::nullopt;
    int y = bounds_.y + listingArea().h;
    int x = bounds_.x + 7 + static_cast<int>(filename_.size()); // 7 = dlugosc "Nazwa: "
    if (x >= bounds_.x + bounds_.w) x = bounds_.x + bounds_.w - 1;
    return std::make_pair(x, y);
}

} // namespace fenrir
