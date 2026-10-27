#include "../apps/texteditor/TextEditorApp.hpp"
#include "../apps/texteditor/TextEditorView.hpp"

#include "test_harness.hpp"

#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/Dialog.hpp"

#include <algorithm>
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
        std::unique_ptr<Dialog> content;
    };
    std::vector<DialogCall> dialogs;
    int saveFileCalls = 0;
    std::string saveFilePath;   // where the next save goes
    std::string saveFileStartDir;
    std::string windowTitle;
    int windowTitleCalls = 0;

    void openFile(const std::string&, OnPick, OnCancel) override {}
    void openFolder(const std::string&, OnPick, OnCancel) override {}
    void openAssociatedFile(const std::string&) override {}
    void openFileWith(const std::string&) override {}
    void closeWindow() override {}
    void setWindowTitle(std::string title) override {
        windowTitle = std::move(title);
        ++windowTitleCalls;
    }

    void notify(std::string, std::string, std::string) override {}
    notifications::Bus& notifications() override { return bus_; }
    void registerBackgroundWorker(std::string, AppContext::BackgroundTick) override {}
    void unregisterBackgroundWorker(const std::string&) override {}
    std::vector<std::string> backgroundWorkers() const override { return {}; }

    notifications::Bus bus_;

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
        call.content = std::move(content);
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

    // Search walks across logical line breaks, selects matches, and wraps to
    // the beginning after reaching the end of the document.
    {
        const std::filesystem::path file = root / "find.txt";
        { std::ofstream out(file); out << "cat scatter\ncatapult cat"; }
        TextArea area;
        area.loadFile(file.string());
        check(area.findNext(U"cat") && area.selectedText() == U"cat",
              "find selects the first match");
        check(area.findNext(U"cat") && area.selectedText() == U"cat",
              "find continues after the current selection");
        check(area.findNext(U"scatter\ncatapult") &&
                  area.selectedText() == U"scatter\ncatapult",
              "find matches across a newline");
        check(area.findNext(U"cat") && area.cursorLineCol() == std::pair{2, 13},
              "find continues at the last document match");
        check(area.findNext(U"cat") && area.cursorLineCol() == std::pair{1, 4},
              "find wraps back to the first match");
        check(area.findNext(U"missing") == false && !area.hasSelection(),
              "a failed find clears the previous selection");
        check(!area.findNext(U""), "empty search text is ignored");
    }

    // Replace-one replaces the current/next match; replace-all is global,
    // non-overlapping, and permits an empty replacement.
    {
        const std::filesystem::path file = root / "replace.txt";
        { std::ofstream out(file); out << "red red\nred"; }
        TextArea area;
        area.loadFile(file.string());
        check(area.replaceNext(U"red", U"blue"), "replace-next finds and replaces a match");
        check(area.selectedText() == U"red", "replace-next selects the following match");
        const std::filesystem::path afterOne = root / "replace-one.txt";
        area.saveFile(afterOne.string());
        check(readFile(afterOne) == "blue red\nred", "replace-next changes only one match");

        area.loadFile(file.string());
        check(area.replaceAll(U"red", U"fox") == 3, "replace-all reports the number of matches");
        const std::filesystem::path afterAll = root / "replace-all.txt";
        area.saveFile(afterAll.string());
        check(readFile(afterAll) == "fox fox\nfox", "replace-all changes every match");
        area.loadFile(file.string());
        check(area.replaceAll(U"red", U"") == 3, "replace-all permits deleting matches");
        const std::filesystem::path afterDelete = root / "replace-delete.txt";
        area.saveFile(afterDelete.string());
        check(readFile(afterDelete) == " \n", "empty replacement removes matches");
        check(area.replaceAll(U"", U"x") == 0, "empty find text cannot replace the buffer");

        area.loadFile(file.string());
        check(area.replaceNext(U"red", U""), "replace-next permits deleting a match");
        const std::filesystem::path afterOneDelete = root / "replace-one-delete.txt";
        area.saveFile(afterOneDelete.string());
        check(readFile(afterOneDelete) == " red\nred",
              "empty replace-next keeps the remaining document intact");
    }

    // The app exposes find/replace through its documented keyboard shortcuts
    // and the Replace all dialog action.
    {
        const std::filesystem::path file = root / "replace-ui.txt";
        { std::ofstream out(file); out << "old value old"; }
        FakeContext ctx;
        apps::TextEditorApp app;
        std::unique_ptr<Widget> widget = app.createFileView(ctx, file.string());
        auto* view = dynamic_cast<apps::TextEditorView*>(widget.get());
        check(view != nullptr, "text editor app creates its editor view");
        auto shortcut = [&](char32_t key) {
            return std::find_if(view->shortcuts().begin(), view->shortcuts().end(),
                                [key](const auto& entry) {
                return entry.first.ctrl && entry.first.key == Key::Char &&
                       entry.first.character == key;
            });
        };
        const auto findShortcut = shortcut(U'f');
        const auto replaceShortcut = shortcut(U'h');
        check(findShortcut != view->shortcuts().end() &&
                  replaceShortcut != view->shortcuts().end(),
              "find and replace keyboard shortcuts are registered");
        if (findShortcut != view->shortcuts().end()) {
            findShortcut->second();
            check(ctx.dialogs.back().content->inputCount() == 1,
                  "Ctrl+F opens the find dialog");
        }
        if (replaceShortcut != view->shortcuts().end()) {
            replaceShortcut->second();
            check(ctx.dialogs.back().content->inputCount() == 2,
                  "Ctrl+H opens the replace dialog");
            ctx.dialogs.back().content->input(0)->setValue(U"old");
            ctx.dialogs.back().content->input(1)->setValue(U"new");
            ctx.dialogs.back().result(1);
            view->save();
            check(readFile(file) == "new value new", "Replace all dialog action updates the file buffer");
        }
    }

    // The view is created before its window is focused, so its initial title
    // must be deferred until draw. Untitled tabs clear the subtitle entirely.
    {
        const std::filesystem::path file = root / "title.txt";
        { std::ofstream out(file); out << "title test"; }
        FakeContext ctx;
        ctx.windowTitle = "Terminal"; // currently focused before the editor window exists
        apps::TextEditorView view(ctx, file.string());
        check(ctx.windowTitleCalls == 0, "editor defers title during construction");
        check(ctx.windowTitle == "Terminal", "editor construction does not rename the previous window");
        view.setBounds(Rect{0, 0, 80, 20});
        Screen screen(80, 20);
        view.draw(screen);
        check(ctx.windowTitle == "title.txt", "editor sets the opened file title on first draw");

        view.newFile();
        check(ctx.windowTitle.empty(), "untitled editor tab clears its subtitle");
    }

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
