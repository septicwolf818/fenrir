#pragma once
#include <functional>
#include <string>

#include "fenrir/widgets/DirectoryView.hpp"
#include "fenrir/widgets/LineEdit.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

enum class FileDialogMode { OpenFile, OpenFolder, SaveFile };

// Jeden widget obslugujacy wszystkie trzy operacje z AppContext (Open/OpenFolder/
// Save). Skladany z PRZEZYTEGNIETEGO UI biblioteki: DirectoryView (lista katalogu),
// LineEdit (pole nazwy), StatusBar (podpowiedzi) — zero wlasnego rysowania list.
// Desktop opakowuje ten widget w zwykle Window; dialog NIE jest osobnym
// mechanizmem, tylko kolejnym oknem frameworka.
class FileDialog : public Widget {
public:
    FileDialog(FileDialogMode mode, std::string startDir, std::string suggestedName);

    std::function<void(const std::string& path)> onConfirm;
    std::function<void()> onCancel;

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& ev) override;
    std::optional<std::pair<int, int>> cursorPosition() const override;

private:
    enum class Zone { List, NameField };

    void confirmSave();
    void syncLayout();

    FileDialogMode mode_;
    Zone zone_ = Zone::List;

    DirectoryView view_;
    LineEdit nameEdit_; // wersja uzywana tylko w trybie SaveFile
    StatusBar status_;

    static constexpr int kStatusRows = 1;
    int nameRows() const { return mode_ == FileDialogMode::SaveFile ? 1 : 0; }
};

} // namespace fenrir
