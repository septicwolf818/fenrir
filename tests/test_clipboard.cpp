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
    // --- Ctrl+Space maps to the selection-mode key ---------------------------
    {
        KeyParser parser;
        const auto ctrl = parser.feed("\x1b[1;5A", 6);
        check(ctrl.size() == 1 && ctrl[0].key == Key::Up && ctrl[0].ctrl,
              "Ctrl+Up keeps its control modifier");

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

    // --- Ctrl+Space selection, copy/cut/paste, and highlighting --------------
    {
        const std::string saved = clipboard::text();
        TextArea area;
        area.setBounds(Rect{0, 0, 40, 10});
        typeText(area, "hello world");
        check(!area.hasSelection(), "no selection after typing");

        KeyParser selectionKeys;
        const auto modifiedArrow = selectionKeys.feed("\x1b[1;2D", 6);
        check(modifiedArrow.size() == 1 && modifiedArrow[0].key == Key::Left,
              "a modified arrow remains a cursor movement key");
        area.handleKey(modifiedArrow[0]);
        check(!area.hasSelection(), "modified arrows do not start a selection");
        area.handleKey(KeyEvent{Key::Right}); // return to the end of the text

        const char nul = '\0';
        const auto space = selectionKeys.feed(&nul, 1);
        check(space.size() == 1 && space[0].ctrl, "Ctrl+Space produces a key event");
        area.handleKey(space[0]);
        for (int i = 0; i < 5; ++i) {
            area.handleKey(KeyEvent{Key::Left});
        }
        check(area.hasSelection(), "arrows start a selection in Ctrl+Space mode");
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
        area.handleKey(space[0]); // selection mode on
        area.handleKey(space[0]); // selection mode off
        area.handleKey(KeyEvent{Key::Left});
        check(!area.hasSelection(), "arrows stop extending the selection when mode is off");
        clipboard::setText(saved); // put the user's clipboard back
    }

}
