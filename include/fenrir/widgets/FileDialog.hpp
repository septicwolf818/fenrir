#pragma once
#include <functional>
#include <string>

#include "fenrir/widgets/DirectoryView.hpp"
#include "fenrir/widgets/LineEdit.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

enum class FileDialogMode { OpenFile, OpenFolder, SaveFile };

// A single widget handling all three AppContext operations (Open/OpenFolder/
// Save). Assembled from the REUSED library UI: DirectoryView (directory list),
// LineEdit (name field), StatusBar (hints) — no custom list drawing of its own.
// Desktop wraps this widget in a regular Window; the dialog is NOT a separate
// mechanism, just another framework window.
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
    LineEdit nameEdit_; // used only in SaveFile mode
    StatusBar status_;

    static constexpr int kStatusRows = 1;
    int nameRows() const { return mode_ == FileDialogMode::SaveFile ? 1 : 0; }
};

} // namespace fenrir
