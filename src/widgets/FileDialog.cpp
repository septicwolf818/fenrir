#include "fenrir/widgets/FileDialog.hpp"

#include <algorithm>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"

namespace fenrir {

FileDialog::FileDialog(FileDialogMode mode, std::string startDir, std::string suggestedName)
    : mode_(mode), view_(std::move(startDir)), nameEdit_(text::toUtf32(suggestedName), U"Nazwa: ") {
    // Lista: Enter na pliku zalezy od trybu dialogu.
    view_.onFileActivate = [this](const std::string& path) {
        if (mode_ == FileDialogMode::OpenFile) {
            if (onConfirm) onConfirm(path);
        } else if (mode_ == FileDialogMode::SaveFile) {
            // Podstawiamy nazwe zaznaczonego pliku i przeskakujemy do pola.
            nameEdit_.setValue(text::toUtf32(fs::nameOf(path)));
            zone_ = Zone::NameField;
        }
        // OpenFolder: Enter na pliku nic nie robi (wybieramy sam katalog, np. 'S').
    };
    nameEdit_.onSubmit = [this](const std::u32string&) { confirmSave(); };
}

void FileDialog::syncLayout() {
    const int statusY = bounds_.y + bounds_.h - kStatusRows;
    const int nameY = statusY - nameRows();
    const Rect listRect{bounds_.x, bounds_.y, bounds_.w, std::max(0, nameY - bounds_.y)};
    view_.setBounds(listRect);
    if (mode_ == FileDialogMode::SaveFile)
        nameEdit_.setBounds(Rect{bounds_.x, nameY, bounds_.w, 1});
    status_.setBounds(Rect{bounds_.x, statusY, bounds_.w, kStatusRows});
}

void FileDialog::confirmSave() {
    const std::u32string& name = nameEdit_.value();
    if (name.empty()) return;
    if (onConfirm) onConfirm(fs::join(view_.currentDir(), text::toUtf8(name)));
}

void FileDialog::draw(Screen& screen) {
    syncLayout();

    view_.setFocused(focused_ && zone_ == Zone::List);
    view_.draw(screen);

    if (mode_ == FileDialogMode::SaveFile) {
        nameEdit_.setFocused(focused_ && zone_ == Zone::NameField);
        nameEdit_.draw(screen);
    }

    std::u32string hint;
    switch (mode_) {
        case FileDialogMode::OpenFile:   hint = U"Enter: wejdz/wybierz plik | Tab: pole nazwy | Esc: anuluj"; break;
        case FileDialogMode::OpenFolder: hint = U"Enter: wejdz | S: wybierz ten folder | Esc: anuluj"; break;
        case FileDialogMode::SaveFile:   hint = U"Enter: wejdz/wpisz nazwe | Tab: pole nazwy | Esc: anuluj"; break;
    }
    status_.setText(std::move(hint));
    status_.draw(screen);
}

KeyEventResult FileDialog::handleKey(const KeyEvent& ev) {
    // Esc = anuluj zawsze (dialog jest modalny; Desktop przekazuje Esc widgetowi
    // pierw — patrz applyWidgetResult — wiec faktycznie tu dociera).
    if (ev.key == Key::Escape) {
        if (onCancel) onCancel();
        return KeyEventResult::Activated;
    }

    // Tab = przelaczenie strefy (wcześniej martwy klawisz — Desktop przekazuje
    // teraz Tab widgetowi przed fallbackiem focusNext).
    if (ev.key == Key::Tab && mode_ == FileDialogMode::SaveFile) {
        zone_ = (zone_ == Zone::List) ? Zone::NameField : Zone::List;
        return zone_ == Zone::NameField ? KeyEventResult::EnteredEdit : KeyEventResult::Consumed;
    }

    if (zone_ == Zone::NameField) {
        // Strzalka gore/dol = powrot do listy (w polu nie ma nawigacji pionowej).
        if (ev.key == Key::Up || ev.key == Key::Down) {
            zone_ = Zone::List;
            return KeyEventResult::Consumed;
        }
        KeyEventResult r = nameEdit_.handleKey(ev);
        return r == KeyEventResult::Ignored ? KeyEventResult::Consumed : r;
    }

    if (mode_ == FileDialogMode::OpenFolder && ev.key == Key::Char && !ev.ctrl &&
        (ev.character == U's' || ev.character == U'S')) {
        if (onConfirm) onConfirm(view_.currentDir());
        return KeyEventResult::Activated;
    }

    const Zone before = zone_;
    KeyEventResult r = view_.handleKey(ev);
    // Enter na pliku w trybie Save przeskoczyl do pola nazwy -> chcemy kursor.
    if (zone_ != before) return KeyEventResult::EnteredEdit;
    return r;
}

std::optional<std::pair<int, int>> FileDialog::cursorPosition() const {
    if (mode_ == FileDialogMode::SaveFile && zone_ == Zone::NameField)
        return nameEdit_.cursorPosition();
    return std::nullopt;
}

} // namespace fenrir
