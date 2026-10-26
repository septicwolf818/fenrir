#pragma once
#include <string>
#include <vector>

#include "HelpPage.hpp"

namespace fenrir::apps::handbook {

// The content of the handbook: tutorials and reference pages for the desktop,
// the modes, the applications and the limits of a text console.
//
// Every page follows the same structure (title, summary, sections), so the
// pages can be read the same way and the table of contents stays predictable.
inline Library buildLibrary() {
    Library lib;

    lib.addCategory({"start", "Getting started"});
    lib.addCategory({"desktop", "The desktop"});
    lib.addCategory({"modes", "Modes"});
    lib.addCategory({"apps", "Applications"});
    lib.addCategory({"reference", "Reference"});

    // --- Getting started -----------------------------------------------------

    lib.add(PageBuilder("start", "welcome", "Welcome to Fenrir")
                .summary("What Fenrir is, and how to read this handbook.")
                .section("What Fenrir is")
                .text("Fenrir is a keyboard-driven workspace that runs directly in a text console "
                      "(a TTY). It provides movable windows and small productivity applications "
                      "without a graphical desktop environment, so it also runs on small machines "
                      "such as a Raspberry Pi 3.")
                .section("Opening the handbook")
                .text("Click Applications on the top bar to open the applications menu and choose Handbook. The handbook "
                      "is an ordinary application: you can move it, resize it, and keep it open "
                      "next to your work.")
                .section("How to read it")
                .bullets({
                    "The left pane lists every page, grouped by category.",
                    "The right pane shows the page that is selected.",
                    "Tab (or Left and Right) switches between the two panes.",
                    "Up and Down move in the contents and scroll the page.",
                    "Ctrl+F searches every page; the contents then show the matches only.",
                    "The numbered links at the end of a page open the related page - press the digit.",
                })
                .seeAlso({"desktop", "modes", "key-reference"})
                .build());

    lib.add(PageBuilder("start", "first-steps", "Your first five minutes")
                .summary("A short tour: open an application, open a file, edit it, and save it.")
                .section("The tour")
                .steps({
                    "Start Fenrir from a text console. It takes over the terminal while it runs "
                    "and restores the terminal when it quits.",
                    "Click Applications on the top bar to start an application - the Text "
                    "Editor is a good place to begin.",
                    "Move the focused window: activate its title bar, use the arrow keys, and "
                    "press Enter or Esc to finish.",
                    "Open a file with Ctrl+O, type something, and save it with Ctrl+S.",
                    "Switch to another window with Alt+Tab, and leave the application with F12.",
                })
                .section("Keys")
                .keys({
                    {"F12", "Leave the application"},
                    {"Alt+Tab", "Next window"},
                    {"Ctrl+O", "Open a file"},
                    {"Ctrl+S", "Save"},
                })
                .section("If a key does nothing")
                .note("The desktop routes the keys according to the current mode. Press Esc to "
                      "step out one interaction level toward the free cursor. Ctrl+Q always quits "
                      "Fenrir, so no mode can trap you.")
                .seeAlso({"modes", "key-reference"})
                .build());

    lib.add(PageBuilder("start", "key-reference", "Key reference")
                .summary("Every global key of the desktop, in one place.")
                .section("Desktop and windows")
                .keys({
                    {"Alt+Tab", "Next window"},
                    {"Ctrl+W", "Close the focused window"},
                    {"Ctrl+Q", "Quit Fenrir"},
                    {"F12", "Leave the application without closing it"},
                    {"Arrows", "Move the desktop cursor"},
                    {"Enter / Space", "Activate what is under the cursor"},
                    {"Esc", "Leave the current mode"},
                })
                .section("Inside an application")
                .keys({
                    {"Ctrl+Q", "Quit Fenrir"},
                    {"Ctrl+W", "Close the window"},
                    {"F12", "Leave the application"},
                    {"Arrows", "Navigate the widgets"},
                    {"Enter / Space", "Activate the selected widget"},
                    {"Esc", "Leave the current mode"},
                })
                .note("A bare Linux console sends far fewer key combinations than a graphical "
                      "terminal: Ctrl+Tab produces no bytes at all, and some keymaps eat the arrow "
                      "keys for the console scrollback. Where that matters, Fenrir uses the plain "
                      "function keys (F3, F4) or ESC plus a digit (Alt+1 .. Alt+9) instead.")
                .seeAlso({"modes", "troubleshooting"})
                .build());

    // --- The desktop ---------------------------------------------------------

    lib.add(PageBuilder("desktop", "desktop", "The desktop")
                .summary("The menu bar, the taskbar, the windows, and the free cursor.")
                .section("The screen")
                .bullets({
                    "The top row is the menu bar. Click \"Applications\" to open the applications menu, "
                    "or \"Notifications\" to open the notification center.",
                    "The bottom row is the taskbar: one handle per window and the clock.",
                    "Everything in between is the desktop area, where the windows live.",
                    "The block cursor is Fenrir's own; the terminal's cursor is hidden while "
                    "Fenrir runs.",
                    "The window title and the taskbar handle reflect the current document, "
                    "directory, or view.",
                    "The Notifications section of the top bar opens the notification center: a "
                    "list of notifications; Delete or Backspace dismisses the selected one.",
                })
                .section("Windows")
                .text("Every application runs in a window. A window has a title bar with the "
                      "application's menus and buttons to close, minimize, and maximize it. "
                      "Windows overlap; the focused one has a bright frame.")
                .section("The cursor")
                .text("In the desktop mode the cursor moves freely over the whole screen. Enter "
                      "or Space activates whatever is under it: the menu bar, a taskbar handle, a "
                      "window button, or a widget of the application below.")
                .seeAlso({"windows", "modes"})
                .build());

    lib.add(PageBuilder("desktop", "windows", "Windows")
                .summary("Move, resize, minimize, maximize, and close the windows.")
                .section("Keys")
                .keys({
                    {"Arrows", "Move or resize, depending on the mode"},
                    {"Enter", "Keep the new position or size"},
                    {"Esc", "Finish moving or resizing"},
                    {"Ctrl+W", "Close the window"},
                })
                .section("The title bar")
                .bullets({
                    "The application name is shown by itself unless the application provides a subtitle; "
                    "then the title is \"App name - subtitle\".",
                    "The tabs are the menus of the application: move the cursor to one and "
                    "activate it.",
                    "Left and Right switch between the menus while a menu is open.",
                    "The buttons on the right close, minimize, and maximize the window.",
                    "Activate the title bar to move the window; activate its border to resize it. "
                    "The title bar buttons close, minimize, and maximize the window.",
                })
                .section("Unsaved changes")
                .note("Closing a window with unsaved changes (the Text Editor, Paint) asks for "
                      "confirmation first. The same guard applies to Ctrl+W and to the close "
                      "button in the title bar.")
                .seeAlso({"desktop", "menubar"})
                .build());

    lib.add(PageBuilder("desktop", "menubar", "Menu bar and taskbar")
                .summary("The applications menu, the window menus, and the taskbar.")
                .section("The menu bar")
                .text("Click Applications on the top bar to open the applications menu: it lists every registered application and "
                      "Quit desktop. Choose one to start it in a new window.")
                .section("The window menus")
                .text("Click a tab in the title bar to open a window menu; in the desktop mode it opens the "
                      "menu of the window under the cursor. Applications add their own tabs, "
                      "such as File, Edit, View, and Format, plus a Help > About entry.")
                .section("The taskbar")
                .text("The taskbar shows one handle per open window - the focused one is "
                      "highlighted. Move the cursor to a handle and press Enter to jump to that "
                      "window. The clock on the right shows the time; the Settings application "
                      "can add the seconds.")
                .seeAlso({"desktop", "windows"})
                .build());

    lib.add(PageBuilder("desktop", "clipboard", "The clipboard")
                .summary("One clipboard for the whole workspace.")
                .section("How it works")
                .text("Fenrir keeps a single clipboard that all the applications share. Text "
                      "copied in the Text Editor can be pasted in another application, and the "
                      "other way round.")
                .section("Where the text is kept")
                .text("A text console has no system clipboard, so the text is stored in "
                      "~/.config/fenrir/clipboard.txt. It is also sent to the terminal with "
                      "OSC 52, so a graphical terminal that supports it can paste the text "
                      "outside of Fenrir as well.")
                .section("Keys in the Text Editor")
                .keys({
                    {"Ctrl+C", "Copy the selection"},
                    {"Ctrl+X", "Cut the selection"},
                    {"Ctrl+V", "Paste"},
                    {"Ctrl+A", "Select all"},
                    {"Ctrl+Space", "Toggle selection mode; arrows extend the selection"},
                })
                .note("Press Ctrl+Space to turn selection mode on. Arrow keys extend the current "
                      "selection; press Ctrl+Space again to turn selection mode off.")
                .seeAlso({"text-editor", "files"})
                .build());

    lib.add(PageBuilder("desktop", "associations", "File associations")
                .summary("Which application opens which kind of file.")
                .section("The defaults")
                .text("Every file type can have a default application. The built-in defaults "
                      "send text and code files to the Text Editor, .fim drawings to Paint, and "
                      "BMP and PNM images to the Photo Viewer.")
                .section("Changing a default")
                .text("In the File Manager, select a file and press Ctrl+O to choose the "
                      "application for it. The choice is stored for that extension in "
                      "~/.config/fenrir/file-associations.ini. A file whose type has no "
                      "association opens the Open with chooser instead of failing.")
                .seeAlso({"file-manager", "files"})
                .build());

    lib.add(PageBuilder("desktop", "files", "Settings and files")
                .summary("Where Fenrir keeps its settings.")
                .section("The files")
                .bullets({
                    "~/.config/fenrir/settings.ini - the desktop settings: theme, clock, cursor "
                    "blink, wallpaper, desktop name.",
                    "~/.config/fenrir/apps/<app>.conf - the preferences of an application (for "
                    "example calendar.conf).",
                    "~/.config/fenrir/clipboard.txt - the shared clipboard.",
                    "~/.config/fenrir/file-associations.ini - the default application per file type.",
                })
                .text("All of them are plain text and can be edited by hand. The Settings "
                      "application is the safe way to change them: it writes the files for you.")
                .seeAlso({"settings-app", "clipboard", "associations"})
                .build());

    // --- Modes ---------------------------------------------------------------

    lib.add(PageBuilder("modes", "modes", "Modes")
                .summary("Fenrir has five modes, and each one decides what the keys do.")
                .section("The modes")
                .bullets({
                    "Desktop - the free cursor moves over the whole screen; Enter and Space "
                    "activate what is under it.",
                    "Keyboard - inside the focused window; the arrows move between the widgets.",
                    "Edit - inside a text field; the keys go straight to the text.",
                    "Window move - the arrows move the focused window.",
                    "Window resize - the arrows resize the focused window.",
                })
                .section("How to leave a mode")
                .text("Esc steps out one interaction level: from Edit to Keyboard, then from "
                      "Keyboard to Desktop; in move and resize modes it ends the operation. F12 "
                      "leaves the application without closing the window or stopping its processes. "
                      "Ctrl+Q always quits Fenrir, whatever the mode is.")
                .seeAlso({"desktop-mode", "keyboard-mode", "edit-mode", "window-modes"})
                .build());

    lib.add(PageBuilder("modes", "desktop-mode", "Desktop mode")
                .summary("The free cursor and the whole screen.")
                .section("How it works")
                .text("The desktop mode is the default one: the cursor moves freely over the "
                      "screen, over the menu bar, the taskbar, and every window.")
                .section("Keys")
                .keys({
                    {"Arrows", "Move the cursor"},
                    {"Enter / Space", "Activate what is under the cursor"},
                    {"Alt+Tab", "Next window"},
                    {"Ctrl+W", "Close the focused window"},
                    {"Ctrl+Q", "Quit Fenrir"},
                    {"F12", "Leave the application"},
                })
                .section("What Enter does")
                .bullets({
                    "On the menu bar: opens the applications menu.",
                    "On the taskbar: focuses that window.",
                    "On a window button: closes, minimizes, or maximizes the window.",
                    "On a window: enters the keyboard mode of that window.",
                    "On a widget of an application (an editor tab, for example): the widget "
                    "handles it.",
                })
                .seeAlso({"modes", "keyboard-mode"})
                .build());

    lib.add(PageBuilder("modes", "keyboard-mode", "Keyboard mode")
                .summary("Working in a window without a mouse.")
                .section("How it works")
                .text("Activating a window, or choosing it from the taskbar, enters its keyboard "
                      "mode: the arrows now move between the widgets of the focused window, and "
                      "Enter or Space activates the selected one.")
                .section("Keys")
                .keys({
                    {"Arrows", "Move the selection"},
                    {"Enter / Space", "Activate the selected widget"},
                    {"Tab", "Next widget (in some applications: the file tree or the other panel)"},
                    {"Esc", "Back to the desktop"},
                })
                .note("In the File Manager, Tab switches between the two panels. In the Text "
                      "Editor, Ctrl+B switches between the file tree and the text.")
                .seeAlso({"modes", "edit-mode"})
                .build());

    lib.add(PageBuilder("modes", "edit-mode", "Edit mode")
                .summary("Typing into a text field.")
                .section("How it works")
                .text("When the focused widget accepts text (the text area of the editor, an "
                      "input field of a dialog), the desktop enters the edit mode and the keys go "
                      "straight to the widget.")
                .section("Keys")
                .keys({
                    {"Characters", "Type"},
                    {"Arrows", "Move the cursor"},
                    {"Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste"},
                    {"Ctrl+Space", "Text Editor: toggle selection mode"},
                    {"Esc", "Back to the keyboard mode of the window"},
                })
                .note("The Terminal application is the exception: it is always in the edit mode, "
                      "because the keys belong to the shell. Esc is forwarded to the shell there "
                      "as well - use F12 to leave the Terminal.")
                .seeAlso({"modes", "clipboard"})
                .build());

    lib.add(PageBuilder("modes", "window-modes", "Move and resize modes")
                .summary("Placing a window by hand.")
                .section("Keys")
                .keys({
                    {"Arrows", "Move or resize by one cell"},
                    {"Enter", "Keep the new position or size"},
                    {"Esc", "Finish moving or resizing"},
                })
                .text("While the window is being moved or resized, its frame follows the arrows. "
                      "Enter or Esc ends the mode and keeps the current position or size.")
                .note("A window never leaves the desktop area: it is moved back inside "
                      "automatically.")
                .seeAlso({"windows", "modes"})
                .build());

    // --- Applications --------------------------------------------------------

    lib.add(PageBuilder("apps", "text-editor", "Text Editor")
                .summary("Edit text and code files with tabs, a file tree, and highlighting.")
                .section("The layout")
                .text("The editor has a tab bar with the open files, an optional file tree on "
                      "the left, the text itself, and a status bar. Ctrl+B shows and hides the "
                      "tree, Ctrl+D opens a folder in it.")
                .section("Files")
                .text("A saved document's filename appears in the window title after the application name. "
                      "An untitled document leaves the application name by itself.")
                .keys({
                    {"Ctrl+N", "New tab"},
                    {"Ctrl+O", "Open a file"},
                    {"Ctrl+S", "Save"},
                    {"Ctrl+R", "Reload the file from disk"},
                    {"Ctrl+T", "Close the tab"},
                    {"F3 / F4", "Previous / next tab"},
                    {"Alt+1 .. Alt+9", "Go to tab N"},
                })
                .section("Editing")
                .keys({
                    {"Ctrl+C / Ctrl+X / Ctrl+V", "Copy / cut / paste"},
                    {"Ctrl+A", "Select all"},
                    {"Ctrl+Space", "Toggle selection mode; arrows extend the selection"},
                    {"Ctrl+E", "Toggle the line wrapping"},
                    {"Ctrl+G", "Toggle the line numbers"},
                    {"Ctrl+Y", "Toggle spaces instead of tab characters"},
                    {"Ctrl+B", "Toggle the file tree"},
                })
                .section("Files changed on disk")
                .text("When a file is changed or removed outside the editor, the status bar shows "
                      "[changed] or [removed]. Saving a file that was changed opens a dialog with "
                      "the choices Overwrite, Save as... and Cancel. Ctrl+R re-reads the file from "
                      "disk and drops the changes of the editor.")
                .section("Syntax highlighting")
                .text("C/C++, JavaScript/TypeScript, Java, Go, Rust, Python, shell, JSON, "
                      "HTML/XML, CSS, INI/TOML/YAML, CMake, and Markdown are highlighted. The "
                      "Format menu switches it off; the choice is remembered.")
                .seeAlso({"clipboard", "files"})
                .build());

    lib.add(PageBuilder("apps", "file-manager", "File Manager")
                .summary("Two panels, copy and move, and the file associations.")
                .section("The layout")
                .text("The File Manager shows two directories side by side. The active panel is "
                      "the highlighted one; the other panel is the target of the copy and the "
                      "move.")
                .section("Keys")
                .keys({
                    {"Tab / Left / Right", "Switch panels"},
                    {"Backspace", "Go to the parent directory"},
                    {"Ctrl+C", "Copy the selection to the other panel"},
                    {"Ctrl+X", "Move the selection to the other panel"},
                    {"Ctrl+R", "Rename the selection"},
                    {"Ctrl+G", "Choose a folder for the active panel"},
                    {"Ctrl+P", "Type a path for the active panel"},
                    {"Ctrl+O", "Choose the application of the selected file"},
                    {"Delete", "Delete the selection"},
                    {"Enter", "Open the selected file"},
                })
                .section("Safety")
                .note("Deleting asks for confirmation. If a destination already contains a file "
                      "with the same name, Fenrir adds a numbered suffix; copying a directory "
                      "into itself or one of its subdirectories is refused.")
                .seeAlso({"associations", "files"})
                .build());

    lib.add(PageBuilder("apps", "calculator", "Calculator")
                .summary("A four-function calculator.")
                .section("How it works")
                .text("Type the numbers and the operators, or move between the buttons with the "
                      "arrow keys and press Enter or Space. The display shows the calculation and "
                      "its result.")
                .section("Keys")
                .keys({
                    {"0-9 . + - * /", "Type the calculation"},
                    {"Arrows", "Move between the buttons"},
                    {"Enter / Space", "Press the selected button"},
                    {"Ctrl+C", "Clear the calculation"},
                })
                .seeAlso({"modes"})
                .build());

    lib.add(PageBuilder("apps", "calendar", "Calendar")
                .summary("Month, week, and day views with events.")
                .section("The views")
                .text("The month view shows a grid with the days of the month, today and the "
                      "cursor highlighted, and the agenda of the selected day below it. The week "
                      "and day views show the same events as blocks in a schedule.")
                .section("Keys")
                .keys({
                    {"Ctrl+N", "New event"},
                    {"Ctrl+T / Ctrl+D", "Week / day view"},
                    {"Ctrl+Up / Ctrl+Down", "Previous / next year"},
                    {"Backspace", "Back from the week or day view to the month"},
                    {"Tab", "Focus the agenda of the month view"},
                    {"Enter", "Open or edit the selected event"},
                    {"Delete", "Delete the selected event"},
                })
                .section("Storage")
                .text("The events are kept in ~/.config/fenrir/apps/calendar.conf.")
                .seeAlso({"files"})
                .build());

    lib.add(PageBuilder("apps", "paint", "Paint")
                .summary("Draw character-cell images with the 16-color palette.")
                .section("How it works")
                .text("The canvas is a grid of terminal cells. The arrow keys move the drawing "
                      "cursor, a printable character paints with the current colors, and Delete "
                      "erases the cell under the cursor.")
                .section("Keys")
                .keys({
                    {"Arrows", "Move the drawing cursor"},
                    {"Characters", "Paint"},
                    {"Delete", "Erase"},
                    {"F3", "Open the color palette"},
                    {"Ctrl+N / Ctrl+O / Ctrl+S", "New / open / save"},
                    {"Ctrl+Z", "Undo"},
                })
                .text("The palette offers every combination of the 16 foreground and background "
                      "colors of the ANSI palette, so a drawing is a grid of colored cells.")
                .seeAlso({"photo-viewer", "associations"})
                .build());

    lib.add(PageBuilder("apps", "photo-viewer", "Photo Viewer")
                .summary("View images with ANSI colors and terminal shading.")
                .section("How it works")
                .text("The image decoder is bundled, so no system image libraries are needed. "
                      "PNG, JPEG, GIF, BMP, TGA, PSD/HDR, PIC, and PNM (PBM, PGM, PPM) are "
                      "supported.")
                .text("Every cell uses a foreground and background color of the 16-color "
                      "palette. Saturated colors are dithered with density blocks, strong "
                      "vertical detail uses half-blocks, and low-chroma areas use a brightness "
                      "ramp.")
                .section("Keys")
                .keys({
                    {"Arrows", "Pan"},
                    {"+ / -", "Zoom in / out"},
                    {"F", "Fit the image to the window"},
                    {"0", "Actual size"},
                    {"Ctrl+O", "Open an image"},
                })
                .seeAlso({"paint"})
                .build());

    lib.add(PageBuilder("apps", "terminal", "Terminal")
                .summary("A real shell in a pseudoterminal.")
                .section("How it works")
                .text("The Terminal application starts $SHELL (or /bin/sh) in a pseudoterminal "
                      "and forwards the keys to it. The window shows a practical VT100/xterm "
                      "subset: colors, cursor movement, and scrolling.")
                .text("Programs that send OSC 0 or OSC 2 title sequences can update the Fenrir window "
                      "subtitle. Fenrir accepts these sequences from the PTY; a program must emit "
                      "them (TERM=linux does not make every program set a title automatically). "
                      "Shell titles that only identify the home directory are ignored, leaving the "
                      "Terminal window with its application name only.")
                .section("Keys")
                .keys({
                    {"PgUp / PgDn", "Scroll the scrollback"},
                    {"Any other key", "Return to the live view"},
                    {"F12", "Back to the desktop, the shell keeps running"},
                    {"Alt+Tab, Ctrl+W", "Stay Fenrir window controls"},
                })
                .note("Esc is sent to the shell, not to Fenrir - the shell decides what to do "
                      "with it. Use F12 to leave the Terminal. On the alternate screen (a "
                      "full-screen program) the scrollback keys belong to that program.")
                .seeAlso({"edit-mode", "troubleshooting"})
                .build());

    lib.add(PageBuilder("apps", "settings-app", "Settings")
                .summary("Desktop, look and feel, notifications, and background services.")
                .section("Tabs")
                .bullets({
                    "Desktop - desktop name, taskbar clock seconds, cursor blink.",
                    "Look & Feel - theme and wallpaper (style, colors, pattern character).",
                    "Notifications - enable or disable application notifications.",
                    "Background - lists the background services currently running.",
                })
                .section("Navigation")
                .keys({
                    {"Tab", "Next tab"},
                    {"Alt+Left / Alt+Right", "Previous / next tab"},
                    {"Arrows", "Move through the settings of the active tab"},
                    {"Enter / Space", "Change the selected setting"},
                })
                .section("The rows")
                .bullets({
                    "Theme - Classic, Midnight, Amber, Forest, or High Contrast.",
                    "Taskbar clock seconds - on or off.",
                    "Desktop name - the name shown in the menu bar.",
                    "Cursor blink - on or off.",
                    "Cursor acceleration - on or off, with a maximum speed.",
                    "App notifications - on or off.",
                    "Wallpaper - on or off, with a style, a background color, a pattern color, and "
                    "a pattern character.",
                })
                .section("Storage")
                .text("The settings are written to ~/.config/fenrir/settings.ini. The theme "
                      "changes the whole interface at once.")
                .seeAlso({"files", "desktop"})
                .build());

    lib.add(PageBuilder("apps", "handbook", "Handbook")
                .summary("This application: the documentation of Fenrir.")
                .section("How it works")
                .text("The Handbook is the built-in documentation. Every page has the same "
                      "structure: a title, a summary, and sections - paragraphs, lists, tables of "
                      "keys, notes, and links to the related pages. The left pane is the table of "
                      "contents, the right pane is the page.")
                .section("Keys")
                .keys({
                    {"Tab / Left / Right", "Switch between the contents and the page"},
                    {"Up / Down", "Move in the contents, scroll the page"},
                    {"PgUp / PgDn", "A page up and down"},
                    {"Ctrl+F", "Search all the pages"},
                    {"1 .. 9", "Open the linked page (the links at the end of a page)"},
                    {"Ctrl+N / Ctrl+P", "Next / previous page"},
                })
                .note("The search keeps the pages that mention the text and shows them in the "
                      "contents; an empty search clears the filter.")
                .seeAlso({"welcome", "key-reference"})
                .build());

    // --- Reference ------------------------------------------------------------

    lib.add(PageBuilder("reference", "troubleshooting", "Troubleshooting")
                .summary("The usual problems on a text console.")
                .section("A key does nothing")
                .text("Check the mode: press Esc to step out one interaction level toward the free "
                      "cursor. Ctrl+Q always quits Fenrir, so no mode can trap you.")
                .section("Alt+Tab does not switch the windows")
                .text("Some consoles do not send Alt+Tab. Use the taskbar instead: move the "
                      "cursor to the handle of the window and press Enter.")
                .section("Selecting text")
                .text("In the Text Editor, press Ctrl+Space to turn selection mode on, then move "
                      "with the arrow keys to extend the selection. Press Ctrl+Space again to "
                      "turn selection mode off.")
                .section("The arrows scroll the console instead")
                .text("The console scrollback is on. Switch it off in the console settings, or "
                      "use the PgUp and PgDn keys of the application (the Terminal application "
                      "uses them for its own scrollback).")
                .section("The window is too small")
                .text("Resize it by activating its border, or maximize it with the title bar button. Every application has a "
                      "minimum size, so it never becomes unusable.")
                .section("The colors look wrong")
                .text("Try another theme in the Settings application. A text console has 16 "
                      "colors only, so the pictures are coarse by nature.")
                .section("Running in a graphical terminal")
                .text("Fenrir works best on a real TTY. In a graphical terminal emulator the "
                      "scrollback and the window title behave a little differently, but "
                      "everything else works the same.")
                .seeAlso({"key-reference", "clipboard"})
                .build());

    return lib;
}

} // namespace fenrir::apps::handbook
