#include "../apps/handbook/HandbookApp.hpp"

#include "test_harness.hpp"

#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/Dialog.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace fenrir;
using fenrir::test::check;

namespace {

// Records what the handbook asks the framework to do.
class FakeContext : public AppContext {
public:
    struct DialogCall {
        std::u32string title;
        std::u32string message;
        OnDialogResult result;
    };
    std::vector<DialogCall> dialogs;
    std::vector<std::unique_ptr<Dialog>> contents; // the dialogs stay alive

    void openFile(const std::string&, OnPick, OnCancel) override {}
    void openFolder(const std::string&, OnPick, OnCancel) override {}
    void openAssociatedFile(const std::string&) override {}
    void openFileWith(const std::string&) override {}
    void closeWindow() override {}
    void saveFile(const std::string&, const std::string&, OnPick, OnCancel) override {}

    void openDialog(std::u32string title, std::unique_ptr<Dialog> content,
                    OnDialogResult onResult) override {
        DialogCall call;
        call.title = std::move(title);
        call.message = content->message();
        call.result = std::move(onResult);
        dialogs.push_back(std::move(call));
        contents.push_back(std::move(content));
    }

    void addFdSource(int, std::function<void()>, std::function<bool()>) override {}
    void removeFdSource(int) override {}

    // Answers the last dialog: types the text into the first input and
    // confirms it (the OK button).
    void answerLast(const std::u32string& text) {
        check(!dialogs.empty(), "a dialog was opened");
        if (contents.back()->input(0)) contents.back()->input(0)->setValue(text);
        dialogs.back().result(0);
    }
};

} // namespace

void runHandbookTests() {
    // --- The content: every page is complete, every link resolves ------------
    {
        const auto library = apps::handbook::buildLibrary();
        check(!library.categories().empty(), "the handbook has categories");
        check(library.pages().size() >= 15, "the handbook has pages");

        std::vector<std::string> ids;
        for (const auto& page : library.pages()) {
            check(!page.id.empty(), "a page has an id");
            check(!page.title.empty(), "a page has a title");
            check(!page.summary.empty(), "a page has a summary");
            check(!page.sections.empty(), "a page has content");
            ids.push_back(page.id);

            bool knownCategory = false;
            for (const auto& category : library.categories())
                if (category.id == page.category) knownCategory = true;
            check(knownCategory, "the category of the page exists");

            for (const auto& section : page.sections) {
                if (section.kind != apps::handbook::SectionKind::SeeAlso) continue;
                check(!section.links.empty(), "a see also section has links");
                for (const auto& link : section.links)
                    check(library.find(link) != nullptr, "the linked page exists");
            }
        }
        std::sort(ids.begin(), ids.end());
        check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "the page ids are unique");
    }

    // --- The builder: the common structure of a page -------------------------
    {
        apps::handbook::PageBuilder builder("cat", "id", "Title");
        builder.summary("Summary").section("Keys").keys({{"Ctrl+N", "New tab"}});
        const auto page = builder.build();
        check(page.category == "cat", "the builder keeps the category");
        check(page.id == "id" && page.title == "Title", "the builder keeps the id and the title");
        check(page.summary == "Summary", "the builder keeps the summary");
        check(page.sections.size() == 1, "the builder makes one section");
        check(page.sections[0].kind == apps::handbook::SectionKind::Keys, "the section has a kind");
        check(page.sections[0].heading == "Keys", "the section keeps the heading");
        check(page.sections[0].keys.size() == 1, "the section keeps the key rows");
    }

    // --- The layout: the title is a band, the keys are a table ----------------
    {
        const auto library = apps::handbook::buildLibrary();
        const auto* page = library.find("key-reference");
        check(page != nullptr, "the key reference page exists");
        const auto lines = apps::handbook::layoutPage(*page, library, 60);
        check(!lines.empty(), "the page is laid out");
        check(lines[0].band, "the first line is the title band");
        check(lines[0].plain() == text::toUtf32(page->title), "the band holds the title");
        for (const auto& line : lines)
            check(static_cast<int>(line.plain().size()) <= 60, "a line fits the width");

        bool foundKey = false, foundAction = false;
        for (const auto& line : lines) {
            const std::u32string plain = line.plain();
            if (plain.find(U"Alt+Tab") != std::u32string::npos) foundKey = true;
            if (plain.find(U"Next window") != std::u32string::npos) foundAction = true;
        }
        check(foundKey && foundAction, "the key table shows the key and the action");
    }

    // --- The layout: a narrow pane falls back to one column -------------------
    {
        const auto library = apps::handbook::buildLibrary();
        const auto* page = library.find("key-reference");
        const auto lines = apps::handbook::layoutPage(*page, library, 20);
        for (const auto& line : lines)
            check(static_cast<int>(line.plain().size()) <= 20, "a line fits the narrow width");
    }

    // Every page fits both the normal and the narrow content pane.
    {
        const auto library = apps::handbook::buildLibrary();
        for (const auto& page : library.pages()) {
            for (const int width : {20, 48, 72}) {
                const auto lines = apps::handbook::layoutPage(page, library, width);
                for (const auto& line : lines)
                    check(static_cast<int>(line.plain().size()) <= width,
                          "every handbook line fits its pane");
            }
        }
    }

    // --- The view: the contents, the search, and the links --------------------
    {
        FakeContext ctx;
        apps::HandbookView view(ctx);
        view.setBounds(Rect{0, 0, 80, 24});

        check(view.currentPageId() == "welcome", "the handbook opens with the welcome page");

        // Down in the contents moves to the next page.
        view.handleKey(KeyEvent{Key::Down});
        check(view.currentPageId() == "first-steps", "the contents move to the next page");

        // The search keeps the pages that mention the text.
        view.handleKey(KeyEvent{Key::Char, U'f', true}); // Ctrl+F
        check(ctx.dialogs.size() == 1, "the search asks for a text");
        ctx.answerLast(U"clipboard");
        check(view.filter() == "clipboard", "the filter is applied");
        check(view.currentPageId() == "clipboard", "the first match is opened");

        // A digit opens the linked page (the links at the end of the page).
        view.handleKey(KeyEvent{Key::Tab}); // to the page
        view.handleKey(KeyEvent{Key::Char, U'1'});
        check(view.currentPageId() == "text-editor", "the link opens the page it points to");

        // Cancel preserves the current filter; a search with no results has a
        // clear empty state and clearing it restores the contents.
        const std::string filterBeforeCancel = view.filter();
        view.handleKey(KeyEvent{Key::Char, U'f', true});
        ctx.dialogs.back().result(-1);
        check(view.filter() == filterBeforeCancel, "canceling search preserves the filter");
        view.handleKey(KeyEvent{Key::Char, U'f', true});
        ctx.answerLast(U"no-such-handbook-page");
        check(view.currentPageId().empty(), "a search with no matches clears the current page");
        view.clearSearch();
        check(view.currentPageId() == "welcome", "clearing the filter restores the first page");
    }

    // The app is registered with a stable id for the Applications menu.
    {
        apps::HandbookApp app;
        check(app.name() == "Handbook", "the app has the Handbook display name");
        check(app.id() == "handbook", "the app has a stable id");
    }

    // --- The view: the page is drawn ------------------------------------------
    {
        FakeContext ctx;
        apps::HandbookView view(ctx);
        view.setBounds(Rect{0, 0, 80, 24});
        Screen screen(80, 24);
        view.draw(screen);

        // The title of the page is drawn in the band above the content.
        bool foundTitle = false;
        for (int x = 0; x < 80 && !foundTitle; ++x) {
            if (screen.at(x, 0).character == U'W') {
                std::u32string text;
                for (int i = x; i < 80; ++i) text += screen.at(i, 0).character;
                foundTitle = text.rfind(U"Welcome to Fenrir", 0) == 0;
            }
        }
        check(foundTitle, "the title of the page is drawn");
    }
}
