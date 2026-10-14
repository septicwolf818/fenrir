#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "fenrir/widgets/Dialog.hpp"

namespace fenrir {

// This is the "dialog API" - the only way an app shows the user a
// file/folder picker or a message/confirmation/prompt.
// It is implemented by Desktop (see desktop/Desktop.hpp); apps get a
// reference to AppContext in App::createView(AppContext&) and know nothing
// about how the dialog is implemented (the framework creates the window,
// sizes it and closes it once a result arrives — the app only gets a callback).
//
// All methods are asynchronous: they open a window and return immediately;
// the result arrives later via the callback (or never, if the user cancels).
class AppContext {
public:
    virtual ~AppContext() = default;

    using OnPick = std::function<void(const std::string& path)>;
    using OnCancel = std::function<void()>;
    // Dialog result: index = button number, -1 = Esc/cancel.
    using OnDialogResult = std::function<void(int index)>;

    // File picker dialog (e.g. Ctrl+O in the editor).
    virtual void openFile(const std::string& startDir, OnPick onPick, OnCancel onCancel = {}) = 0;

    // Folder picker dialog (e.g. "Go to folder..." in the File Manager).
    virtual void openFolder(const std::string& startDir, OnPick onPick, OnCancel onCancel = {}) = 0;

    // Save dialog: browsing + file name field (e.g. "Save as...").
    virtual void saveFile(const std::string& startDir, const std::string& suggestedName,
                           OnPick onPick, OnCancel onCancel = {}) = 0;

    // Open a file using its saved global extension association. If none exists,
    // the desktop presents the Open With chooser.
    virtual void openAssociatedFile(const std::string& path) = 0;
    // Choose an app for this file and save that choice as the extension default.
    virtual void openFileWith(const std::string& path) = 0;

    // Opens a dialog window with a Dialog widget (message / confirmation /
    // prompt). The framework sizes the window, closes it once a result
    // arrives and returns focus to the window that raised it.
    virtual void openDialog(std::u32string title, std::unique_ptr<Dialog> content,
                            OnDialogResult onResult) = 0;

    // --- Asynchronous data sources (e.g. a terminal PTY) --------------------
    // Registers an fd polled once per tick (poll POLLIN, non-blocking); onReady
    // is called when data is available on fd. The source MUST be removed
    // (removeFdSource) BEFORE fd is closed — the widget destructor is the place.
    virtual void addFdSource(int fd, std::function<void()> onReady) = 0;
    virtual void removeFdSource(int fd) = 0;

    // --- High-level helpers (build a Dialog and open it via openDialog) ---

    // "OK" message box (e.g. "About Calculator").
    void showMessage(std::u32string text, std::function<void()> onOk = {}) {
        auto dlg = std::make_unique<Dialog>(std::move(text), Dialog::Buttons::Ok);
        openDialog(U"Information", std::move(dlg),
                   [onOk = std::move(onOk)](int idx) {
                       if (idx == 0 && onOk) onOk();
                   });
    }

    // Consistent About dialog shared by every app's Help > About menu.
    void showAbout(std::u32string appName, std::u32string description) {
        std::u32string message = U"Fenrir " + appName + U"\nVersion 1.0\n\n" + description;
        auto dlg = std::make_unique<Dialog>(std::move(message), Dialog::Buttons::Ok);
        openDialog(U"About " + std::move(appName), std::move(dlg), [](int) {});
    }

    // Yes/no question (OK/Cancel): true = OK, false = Cancel/Esc.
    void confirm(std::u32string text, std::function<void(bool)> onAnswer) {
        auto dlg = std::make_unique<Dialog>(std::move(text), Dialog::Buttons::OkCancel);
        openDialog(U"Confirmation", std::move(dlg),
                   [onAnswer = std::move(onAnswer)](int idx) {
                       if (onAnswer) onAnswer(idx == 0);
                   });
    }

    // Prompt with a text field: nullopt = cancel/Esc, value = entered text.
    // The LineEdit pointer lives until the window closes — the callback is
    // invoked exactly then (just before closing), so reading the value is safe.
    void prompt(std::u32string title, std::u32string message, std::u32string label,
                std::u32string initial,
                std::function<void(std::optional<std::u32string>)> onText) {
        auto dlg = std::make_unique<Dialog>(std::move(message), Dialog::Buttons::OkCancel);
        LineEdit* field = dlg->setInput(std::move(label), std::move(initial));
        openDialog(std::move(title), std::move(dlg),
                   [field, onText = std::move(onText)](int idx) {
                       if (!onText) return;
                       if (idx == 0) onText(field->value());
                       else onText(std::nullopt);
                   });
    }
};

} // namespace fenrir
