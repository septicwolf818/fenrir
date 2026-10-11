#pragma once
#include <functional>
#include <string>

#include "fenrir/widgets/DirectoryListing.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

enum class FileDialogMode { OpenFile, OpenFolder, SaveFile };

// Jeden widget obslugujacy wszystkie trzy operacje z AppContext (Open/OpenFolder/
// Save). Komponuje DirectoryListing - DOKLADNIE TA SAMA klasa, ktorej uzywa
// FileManagerView do zwyklego przegladania. Desktop (patrz app/AppContext.hpp)
// opakowuje ten widget w zwykle Window - dialog NIE jest osobnym mechanizmem,
// tylko kolejnym oknem frameworka.
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

    FileDialogMode mode_;
    DirectoryListing listing_;
    std::u32string filename_;
    Zone zone_ = Zone::List;

    static constexpr int kHintRows = 1;

    int nameFieldRows() const { return mode_ == FileDialogMode::SaveFile ? 1 : 0; }
    Rect listingArea() const;
    void confirmSave();
};

} // namespace fenrir
