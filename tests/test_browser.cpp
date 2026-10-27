#include "../apps/browser/BrowserNetwork.hpp"
#include "../apps/browser/BrowserPage.hpp"
#include "../apps/browser/BrowserView.hpp"

#include "fenrir/core/Screen.hpp"
#include "fenrir/widgets/Button.hpp"
#include "fenrir/widgets/HorizontalLayout.hpp"
#include "fenrir/widgets/LineEdit.hpp"

#include "test_harness.hpp"

using namespace fenrir;
using fenrir::test::check;

namespace {
class BrowserTestContext final : public AppContext {
public:
    void openFile(const std::string&, OnPick, OnCancel) override {}
    void openFolder(const std::string&, OnPick, OnCancel) override {}
    void saveFile(const std::string&, const std::string&, OnPick, OnCancel) override {}
    void openAssociatedFile(const std::string&) override {}
    void openFileWith(const std::string&) override {}
    void notify(std::string, std::string, std::string) override {}
    notifications::Bus& notifications() override { return bus_; }
    void registerBackgroundWorker(std::string, BackgroundTick) override {}
    void unregisterBackgroundWorker(const std::string&) override {}
    std::vector<std::string> backgroundWorkers() const override { return {}; }
    void openDialog(std::u32string, std::unique_ptr<Dialog>, OnDialogResult) override {}
    void closeWindow() override {}
    void setWindowTitle(std::string value) override { windowTitle = std::move(value); }
    void addFdSource(int, std::function<void()>, std::function<bool()>) override {}
    void removeFdSource(int) override {}

    std::string windowTitle;

private:
    notifications::Bus bus_;
};
} // namespace

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

    {
        BrowserTestContext context;
        apps::browser::BrowserView browser(context);
        check(!browser.editable(), "browser opens in keyboard navigation mode, not text edit mode");
    }

    // Browser history uses Ctrl+Arrow because Alt+Arrow switches Linux TTYs.
    {
        BrowserTestContext context;
        apps::browser::BrowserView browser(context);
        browser.setBounds(Rect{0, 0, 80, 24});
        Screen screen(80, 24);
        std::string currentAddress;
        const auto navigate = [&](const std::string& address) {
            (void)browser.handleKey(KeyEvent{Key::Char, U'l', true});
            for (std::size_t i = 0; i < currentAddress.size(); ++i)
                (void)browser.handleKey(KeyEvent{Key::Backspace});
            for (char c : address)
                (void)browser.handleKey(KeyEvent{Key::Char, static_cast<char32_t>(c)});
            (void)browser.handleKey(KeyEvent{Key::Enter});
            currentAddress = address;
            browser.draw(screen);
        };
        navigate("https://example.test/first");
        navigate("https://example.test/second");
        check(context.windowTitle == "https://example.test/second",
              "address navigation updates the active browser page");

        (void)browser.handleKey(KeyEvent{Key::Left, 0, false, true});
        browser.draw(screen);
        check(context.windowTitle == "https://example.test/second",
              "Alt+Left is not intercepted as browser history navigation");

        (void)browser.handleKey(KeyEvent{Key::Left, 0, true});
        browser.draw(screen);
        check(context.windowTitle == "https://example.test/first",
              "Ctrl+Left navigates backward");
        (void)browser.handleKey(KeyEvent{Key::Right, 0, true});
        browser.draw(screen);
        check(context.windowTitle == "https://example.test/second",
              "Ctrl+Right navigates forward");
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
        const auto linkCursor = page.cursorPosition();
        check(linkCursor.has_value(), "focused page link exposes a Fenrir cursor position");
        if (linkCursor) {
            check(!page.clickAt(linkCursor->first, linkCursor->second),
                  "desktop cursor activation enters browser keyboard mode");
            check(page.takePendingNavigation().empty(),
                  "desktop mode does not activate a page link");
            check(page.handleKey(KeyEvent{Key::Enter}) == KeyEventResult::Activated,
                  "browser keyboard cursor activates the focused link with Enter");
            check(page.takePendingNavigation() == "https://example.test/next",
                  "keyboard-mode Enter requests navigation");
        }

        check(page.loadHtml(html, "https://example.test/start"),
              "fixture can be reloaded for Enter activation");
        check(page.focusLink(), "link can be focused again after reload");
        (void)page.activateFocusedLink();
        check(page.takePendingNavigation() == "https://example.test/next",
              "Enter activation produces a resolved navigation URL");

        const auto hovered = page.cursorPosition();
        check(hovered.has_value(), "focused link positions the in-page cursor");
        if (hovered) {
            check(page.loadHtml(html, "https://example.test/start"),
                  "fixture reloads before virtual-cursor activation");
            check(!page.clickAt(hovered->first, hovered->second),
                  "desktop cursor enters the page without clicking its link");
            check(page.handleKey(KeyEvent{Key::Enter}) == KeyEventResult::Activated,
                  "Enter activates the link under the browser's own cursor");
            check(page.takePendingNavigation() == "https://example.test/next",
                  "virtual-cursor activation requests navigation");
        }

        check(!page.clickAt(47, 10), "clicking page background enters browser interaction mode");
        check(page.cursorPosition() == std::make_pair(47, 10),
              "background click leaves the in-page cursor at the pointer location");
    }

    // QuickJS executes both classic scripts and ES modules through the
    // restricted DOM bridge; generated markup is inserted before layout.
    {
        apps::browser::BrowserPage page;
        page.setBounds(Rect{0, 0, 48, 10});
        const std::string html =
            "<html><body><div id='classic'></div><div id='module'></div>"
            "<script>document.getElementById('classic').textContent='CLASSIC-RAN';</script>"
            "<script type='module'>document.getElementById('module').innerHTML='<p>MODULE-RAN</p>';</script>"
            "</body></html>";
        check(page.loadHtml(html, "https://example.test/scripts"), "script fixture parses");
        Screen screen(48, 10);
        page.draw(screen);
        check(page.cursorPosition() == std::make_pair(0, 0),
              "page keyboard cursor starts inside the browser viewport");
        std::string rendered;
        for (const Cell& cell : screen.cells())
            if (cell.character < 128) rendered.push_back(static_cast<char>(cell.character));
        check(rendered.find("CLASSIC-RAN") != std::string::npos,
              "classic JavaScript DOM mutation renders");
        check(rendered.find("MODULE-RAN") != std::string::npos,
              "module JavaScript DOM mutation renders");
    }

    // Browser forms have a visible terminal-cell treatment and enter text-edit
    // mode only after a field is selected.
    {
        apps::browser::BrowserPage page;
        page.setBounds(Rect{0, 0, 48, 10});
        check(page.loadHtml(
                  "<html><body><input type='text' value='hello' placeholder='Type here'></body></html>",
                  "https://example.test/form"),
              "text input fixture parses");
        Screen screen(48, 10);
        page.draw(screen);
        std::string rendered;
        bool sawInputFrame = false;
        for (const Cell& cell : screen.cells()) {
            if (cell.character < 128) rendered.push_back(static_cast<char>(cell.character));
            sawInputFrame = sawInputFrame || cell.character == U'┌';
        }
        check(sawInputFrame, "text fields render with a visible frame");
        check(rendered.find("hello") != std::string::npos,
              "text field value is visible inside its frame");
        check(page.focusNextControl(), "Tab traversal reaches a text field");
        check(page.inputFocused(), "focused text field is exposed for edit-mode routing");
        check(page.handleKey(KeyEvent{Key::Enter}) == KeyEventResult::EnteredEdit,
              "Enter requests edit mode for a focused text field");
        check(page.handleKey(KeyEvent{Key::Char, U'!'} ) == KeyEventResult::Consumed,
              "text input consumes typed characters");
        screen.clear();
        page.draw(screen);
        rendered.clear();
        for (const Cell& cell : screen.cells())
            if (cell.character < 128) rendered.push_back(static_cast<char>(cell.character));
        check(rendered.find("hello!") != std::string::npos,
              "typed text is rendered in the focused input");
    }

    // Keep dynamically generated grid links navigable in a narrow viewport
    // without replacing LiteHTML's page rendering with a forced block layout.
    {
        apps::browser::BrowserPage page;
        page.setBounds(Rect{0, 0, 60, 12});
        check(page.loadHtml(
                  "<html><head><style>#cards{display:grid;"
                  "grid-template-columns:repeat(2,minmax(0,1fr));gap:24px}"
                  ".card{display:flex;flex-direction:column;min-height:240px}</style>"
                  "</head><body><section><h2>Items</h2><div id='cards'></div>"
                  "<script>document.getElementById('cards').innerHTML="
                  "'<article class=\"card\"><h3>Alpha</h3><div><a "
                  "href=\"https://example.test/alpha\">Open alpha</a></div></article>' +"
                  "'<article class=\"card\"><h3>Beta</h3><div><a "
                  "href=\"https://example.test/beta\">Open beta</a></div></article>';</script>"
                  "</section></body></html>",
                  "https://example.test/"),
              "dynamic grid fixture parses");
        check(page.focusNextControl(), "first grid link is keyboard-focusable");
        check(page.focusedLinkUrl() == "https://example.test/alpha",
              "first link stays attached to its dynamically rendered card");
        auto pointer = page.cursorPosition();
        check(pointer.has_value(), "link focus positions the browser's own cursor");
        if (pointer) {
            check(!page.clickAt(pointer->first, pointer->second),
                  "desktop activation enters the page without following the link");
            check(page.handleKey(KeyEvent{Key::Enter}) == KeyEventResult::Activated,
                  "browser Keyboard-mode Enter activates the grid link");
            check(page.takePendingNavigation() == "https://example.test/alpha",
                  "Enter follows the first grid link");
        }
        check(page.focusNextControl(), "next grid link is keyboard-focusable");
        check(page.focusedLinkUrl() == "https://example.test/beta",
              "narrow layout keeps the next card in reading order");
        pointer = page.cursorPosition();
        if (pointer) {
            check(!page.clickAt(pointer->first, pointer->second),
                  "desktop activation enters the next card without following it");
            check(page.handleKey(KeyEvent{Key::Enter}) == KeyEventResult::Activated,
                  "browser cursor activates the next card with Enter");
            check(page.takePendingNavigation() == "https://example.test/beta",
                  "Enter follows the next grid link");
        }
    }

    // Fixed decorative layers (like the portfolio's grid/glow backgrounds)
    // must not paint opaque ANSI cells over text after the page scrolls.
    {
        std::string html =
            "<html><head><style>body{background:#070814;color:#fff}"
            ".grid{position:fixed;inset:0;z-index:0;background:"
            "linear-gradient(rgba(79,110,247,.18),transparent)}"
            ".content{position:relative;z-index:1}</style></head><body>"
            "<div class='grid'></div><main class='content'>";
        for (int i = 0; i < 30; ++i)
            html += "<p>SCROLL-LINE-" + std::to_string(i) + " visible page text</p>";
        html += "</main></body></html>";

        apps::browser::BrowserPage page;
        page.setBounds(Rect{0, 0, 48, 10});
        check(page.loadHtml(std::move(html), "https://example.test/"),
              "fixed-background scrolling fixture lays out");
        page.handleKey(KeyEvent{Key::PageDown});
        Screen screen(48, 10);
        page.draw(screen);
        bool sawScrolledText = false;
        for (const Cell& cell : screen.cells())
            if (cell.character >= U'A' && cell.character <= U'Z') sawScrolledText = true;
        check(sawScrolledText, "fixed background does not cover scrolled page text");

        std::string documentText;
        for (int step = 0; step < 30; ++step) {
            screen.clear();
            page.draw(screen);
            for (const Cell& cell : screen.cells())
                if (cell.character < 128) documentText.push_back(static_cast<char>(cell.character));
            const int before = page.scrollRow();
            page.handleKey(KeyEvent{Key::PageDown});
            if (page.scrollRow() == before) break;
        }
        check(documentText.find("SCROLL-LINE-29") != std::string::npos,
              "scrolling reaches text beyond the initial viewport");

        page.handleKey(KeyEvent{Key::Home});
        check(!page.clickAt(24, 9), "desktop cursor enters the page at its bottom edge");
        page.handleKey(KeyEvent{Key::Down});
        check(page.scrollRow() == 1 && page.cursorPosition() == std::make_pair(24, 9),
              "browser cursor scrolls the page at its bottom edge in Keyboard mode");
        page.handleKey(KeyEvent{Key::End});
        check(!page.clickAt(24, 0), "desktop cursor enters the page at its top edge");
        const int beforeTopScroll = page.scrollRow();
        page.handleKey(KeyEvent{Key::Up});
        check(page.cursorPosition() == std::make_pair(24, 0) &&
                  page.scrollRow() == beforeTopScroll - 1,
              "browser cursor scrolls back at the top edge in Keyboard mode");
    }
}
