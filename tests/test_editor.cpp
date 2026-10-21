#include "../apps/texteditor/TextEditorView.hpp"

#include "test_harness.hpp"

#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/Dialog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace fenrir;
using fenrir::test::check;


// Records what the editor asks the framework to do.
class FakeContext : public AppContext {
public:
    struct DialogCall {
        std::u32string title;
        std::u32string message;
        OnDialogResult result;
    };
    std::vector<DialogCall> dialogs;
    int saveFileCalls = 0;
    std::string saveFilePath;   // where the next save goes
    std::string saveFileStartDir;

    void openFile(const std::string&, OnPick, OnCancel) override {}
    void openFolder(const std::string&, OnPick, OnCancel) override {}
    void openAssociatedFile(const std::string&) override {}
    void openFileWith(const std::string&) override {}
    void closeWindow() override {}

    void saveFile(const std::string& startDir, const std::string&, OnPick onPick, OnCancel) override {
        ++saveFileCalls;
        saveFileStartDir = startDir;
        if (onPick) onPick(saveFilePath);
    }

    void openDialog(std::u32string title, std::unique_ptr<Dialog> content,
                    OnDialogResult onResult) override {
        DialogCall call;
        call.title = std::move(title);
        call.message = content->message();
        call.result = std::move(onResult);
        dialogs.push_back(std::move(call));
    }

    // The editor does not use fd sources, but they are part of the contract.
    void addFdSource(int, std::function<void()>, std::function<bool()>) override {}
    void removeFdSource(int) override {}
};

static std::string readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void runEditorTests() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "fenrir_editor_conflict";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // --- The file was changed outside the editor: a dialog asks what to do ---
    {
        const std::filesystem::path file = root / "doc.txt";
        {
            std::ofstream out(file, std::ios::binary);
            out << "original";
        }
        FakeContext ctx;
        apps::TextEditorView view(ctx, file.string());
        // The user types something in the editor.
        view.handleKey(KeyEvent{Key::Char, U'!'});

        // Someone else changes the file.
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << "EXTERNAL-EDIT-CONTENT";
        }

        view.save();
        check(ctx.dialogs.size() == 1, "a conflict dialog is opened");
        const std::string message(ctx.dialogs[0].message.begin(), ctx.dialogs[0].message.end());
        check(message.find("doc.txt") != std::string::npos, "the dialog names the file");
        check(message.find("EXTERNAL-EDIT-CONTENT") == std::string::npos,
              "the dialog does not show the other version's content");

        // "Overwrite" writes the editor's version to the file.
        ctx.dialogs[0].result(0);
        // The character was typed at the cursor (the start of the loaded file).
        check(readFile(file) == "!original", "overwrite writes the editor's content");
    }

    // --- The file was removed: saving asks for a new name ------------------
    {
        const std::filesystem::path file = root / "gone.txt";
        {
            std::ofstream out(file, std::ios::binary);
            out << "content";
        }
        FakeContext ctx;
        apps::TextEditorView view(ctx, file.string());
        view.handleKey(KeyEvent{Key::Char, U'?'});

        std::filesystem::remove(file);
        view.save();
        check(ctx.dialogs.empty(), "no conflict dialog for a removed file");
        check(ctx.saveFileCalls == 1, "a save dialog is opened for a removed file");

        // The user picks a name in the save dialog.
        const std::filesystem::path target = root / "gone-new.txt";
        ctx.saveFilePath = target.string();
        view.save();
        check(ctx.saveFileCalls == 2, "saving again opens the save dialog");
        check(readFile(target) == "?content", "the buffer is written under the new name");
    }

    // --- Reload from disk: the buffer takes the content of the file ---------
    {
        const std::filesystem::path file = root / "reload.txt";
        {
            std::ofstream out(file, std::ios::binary);
            out << "on disk";
        }
        FakeContext ctx;
        apps::TextEditorView view(ctx, file.string());
        // The user changes the buffer, then someone else changes the file.
        view.handleKey(KeyEvent{Key::Char, U'!'});
        {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << "new on disk";
        }

        view.reloadFromDisk();
        // The buffer was dirty -> a confirmation is asked first.
        check(ctx.dialogs.size() == 1, "reloading a dirty buffer asks for confirmation");
        ctx.dialogs[0].result(0); // OK
        check(!view.dirty(), "the reloaded buffer is not dirty");

        // Write the buffer out under another name to see what it holds.
        const std::filesystem::path copy = root / "reload-copy.txt";
        ctx.saveFilePath = copy.string();
        view.saveAs();
        check(readFile(copy) == "new on disk", "the reload took the content from disk");
    }

    std::filesystem::remove_all(root);
}
