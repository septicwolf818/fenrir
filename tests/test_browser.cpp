#include "../apps/browser/BrowserNetwork.hpp"
#include "../apps/browser/BrowserPage.hpp"

#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/Button.hpp"
#include "fenrir/widgets/HorizontalLayout.hpp"
#include "fenrir/widgets/LineEdit.hpp"

#include "test_harness.hpp"

using namespace fenrir;
using fenrir::test::check;

void runBrowserTests() {
    check(apps::browser::normalizeUrl("example.com") == "https://example.com",
          "bare host becomes an HTTPS URL");
    check(apps::browser::resolveUrl("https://example.test/a/b", "../next") ==
              "https://example.test/next",
          "relative links resolve against the page URL");
    check(apps::browser::normalizeUrl("javascript:alert(1)").empty(),
          "non-HTTP schemes are rejected");

    // The reusable horizontal layout gives toolbar controls fixed widths and
    // lets the address field take the remaining space.
    {
        HorizontalLayout row;
        int activated = 0;
        auto button = std::make_unique<Button>(U"Go");
        button->onActivate = [&] { ++activated; };
        row.addFixed(std::move(button), 8);
        auto address = std::make_unique<LineEdit>();
        LineEdit* addressPtr = address.get();
        const int addressSlot = row.addFlexible(std::move(address));
        row.setBounds(Rect{2, 1, 42, 1});
        Screen screen(50, 4);
        row.draw(screen);
        check(row.child(0)->bounds().w == 8 && row.child(addressSlot)->bounds().w == 34,
              "horizontal layout applies fixed and flexible widths");

        row.focus(addressSlot);
        row.handleKey(KeyEvent{Key::Char, U'x'});
        check(addressPtr->value() == U"x", "horizontal layout routes editing to focused child");
        row.handleKey(KeyEvent{Key::Tab});
        check(row.focusedIndex() == 0, "horizontal layout tabs between controls");
        check(row.handleKey(KeyEvent{Key::Enter}) == KeyEventResult::Activated && activated == 1,
              "horizontal layout activates the focused control");
    }

    // Parse HTML, apply inline CSS, paint the page through Fenrir's Screen, and
    // use the virtual cursor path to activate a keyboard-focused link.
    {
        apps::browser::BrowserPage page;
        page.setBounds(Rect{1, 1, 48, 10});
        const std::string html =
            "<!doctype html><html><head><title>Fixture</title>"
            "<style>body{background:#fff;color:#000}a{color:#00f}</style></head>"
            "<body><h1>Hello web</h1><p><a href='/next'>next page</a></p></body></html>";
        check(page.loadHtml(html, "https://example.test/start"), "litehtml parses a local fixture");
        check(page.title() == "Fixture", "document title is exposed to browser chrome");

        Screen screen(60, 16);
        page.draw(screen);
        bool sawText = false;
        for (const Cell& cell : screen.cells()) sawText = sawText || cell.character == U'H';
        check(sawText, "HTML text is drawn into Fenrir screen cells");
        check(screen.at(2, 2).bg == colors::BrightWhite,
              "CSS page background maps to an ANSI background color");
        check(page.focusLink(), "Tab-style focus finds a page link");
        check(page.cursorPosition().has_value(), "focused page link exposes a Fenrir cursor position");
        (void)page.activateFocusedLink();
        check(page.takePendingNavigation() == "https://example.test/next",
              "activating a link produces a resolved navigation URL");
    }
}
