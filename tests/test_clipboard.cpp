#include "test_harness.hpp"

#include "fenrir/core/Clipboard.hpp"
#include "fenrir/core/Screen.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/input/KeyParser.hpp"
#include "fenrir/widgets/TextArea.hpp"

#include <iostream>
#include <string>

using namespace fenrir;
using fenrir::test::check;


static void typeText(TextArea& area, const std::string& utf8) {
    for (const char c : utf8)
        area.handleKey(KeyEvent{Key::Char, static_cast<char32_t>(c)});
}

void runClipboardTests() {
    // --- The key parser understands the shift modifier -----------------------
    {
        KeyParser parser;
        const auto events = parser.feed("\x1b[1;2A", 6);
        check(events.size() == 1, "one key from the sequence");
        check(events[0].key == Key::Up, "Shift+Up is an Up key");
        check(events[0].shift, "the shift modifier is set");
        check(!events[0].ctrl && !events[0].alt, "no other modifier");

        const auto ctrl = parser.feed("\x1b[1;5A", 6);
        check(ctrl.size() == 1 && ctrl[0].ctrl && !ctrl[0].shift, "Ctrl+Up keeps ctrl only");

        const auto both = parser.feed("\x1b[1;6A", 6);
        check(both.size() == 1 && both[0].ctrl && both[0].shift, "Ctrl+Shift+Up has both");

        const char nul = '\0';
        const auto space = parser.feed(&nul, 1);
        check(space.size() == 1 && space[0].ctrl && space[0].character == U'`',
              "Ctrl+Space's NUL sequence is represented for the selection fallback");
    }

    // --- The clipboard keeps the text between applications -------------------
    {
        const std::string saved = clipboard::text(); // restore it at the end
        clipboard::setText("clipboard content");
        check(clipboard::text() == "clipboard content", "the clipboard keeps the text");
        check(!clipboard::empty(), "the clipboard is not empty");
        clipboard::clear();
        check(clipboard::empty(), "the clipboard can be cleared");
        clipboard::setText(saved); // put the user's clipboard back
    }

    // --- Base64 (OSC 52) ---------------------------------------------------
    {
        check(clipboard::base64Encode("") == "", "empty input");
        check(clipboard::base64Encode("a") == "YQ==", "one byte");
        check(clipboard::base64Encode("ab") == "YWI=", "two bytes");
        check(clipboard::base64Encode("abc") == "YWJj", "three bytes");
        check(clipboard::base64Encode("hello world") == "aGVsbG8gd29ybGQ=", "a longer text");
    }

    // --- Selecting text with Shift+Arrow and copying it ---------------------
    {
        const std::string saved = clipboard::text();
        TextArea area;
        area.setBounds(Rect{0, 0, 40, 10});
        typeText(area, "hello world");
        check(!area.hasSelection(), "no selection after typing");

        KeyParser selectionKeys;
        for (int i = 0; i < 5; ++i) {
            const auto events = selectionKeys.feed("\x1b[1;2D", 6); // Shift+Left
            check(events.size() == 1 && events[0].shift && events[0].key == Key::Left,
                  "parser emits Shift+Left");
            area.handleKey(events[0]);
        }
        check(area.hasSelection(), "Shift+Left starts a selection");
        check(area.selectedText() == U"world", "the selection covers the last word");
        Screen screen(40, 10);
        area.draw(screen);
        check(screen.at(6, 0).fg == theme::active.accentText &&
                  screen.at(6, 0).bg == theme::active.accentBg,
              "the selected text is visibly highlighted");

        check(area.copySelection(), "the selection is copied");
        check(clipboard::text() == "world", "the clipboard holds the selection");

        check(area.cutSelection(), "the selection is cut");
        check(area.selectedText().empty(), "nothing is selected after the cut");
        check(area.dirty(), "cutting marks the buffer dirty");

        area.pasteClipboard();
        check(!area.dirty() ? false : true, "pasting changes the buffer");

        // Select all and copy it.
        area.handleKey(KeyEvent{Key::Char, U'a', true}); // Ctrl+A
        check(area.hasSelection(), "Ctrl+A selects everything");
        check(area.selectedText() == U"hello world", "the whole buffer is selected");
        clipboard::setText(saved); // put the user's clipboard back
    }

    // The bare Linux console may not include Shift in arrow-key bytes. Ctrl+
    // Space toggles selection mode there; arrows then extend the selection.
    {
        TextArea area;
        area.setBounds(Rect{0, 0, 40, 10});
        typeText(area, "hello world");
        const char nul = '\0';
        KeyParser parser;
        const auto keys = parser.feed(&nul, 1);
        check(keys.size() == 1, "Ctrl+Space produces a key event");
        area.handleKey(keys[0]);
        for (int i = 0; i < 5; ++i)
            area.handleKey(KeyEvent{Key::Left});
        check(area.hasSelection() && area.selectedText() == U"world",
              "Ctrl+Space selection mode extends with plain arrows");
    }

}
