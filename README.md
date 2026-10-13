# Fenrir

Fenrir is a lightweight, keyboard-driven workspace that runs directly in a
Linux text console (TTY). It provides movable windows and small productivity
apps without starting a graphical desktop environment. The project is also
buildable on macOS for development and testing.

Fenrir is intended to stay useful on resource-constrained hardware, including
devices such as the Raspberry Pi 3. It focuses on workspace and productivity
features rather than system administration.

## Features

- Full-screen terminal UI with windowed, keyboard, and text-editing modes.
- Window movement, resizing, minimization, and maximize/restore.
- Built-in apps: Text Editor, File Manager, Calculator, Settings, Calendar,
  Paint, and Terminal.
- Reusable widgets for menus, dialogs, lists, text input, file selection,
  directory trees, tabs, and status bars.
- A terminal window that starts the user's shell in a pseudoterminal and
  supports a practical VT100/xterm subset.
- Persistent desktop settings and per-app preferences under
  `~/.config/fenrir/`.

## Build and run

Requirements: CMake 3.16 or newer, a C++20 compiler, a POSIX environment,
and a UTF-8 locale for box-drawing and other non-ASCII text.
On Linux, `forkpty` is linked from `libutil`; macOS provides it through the
system libraries.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/fenrir
```

You can also use `make build` and `make run`. For the intended Linux setup,
run Fenrir from a logged-in text console (TTY). It takes over the terminal
while running and restores it when you exit.

## Keyboard reference

This reference is kept in the documentation rather than in the application
menu title. App menus also list their available actions.

### Desktop and windows

| Key | Action |
| --- | --- |
| `F1` | Open the Applications menu |
| `F2` | Open the focused window's menu |
| `Alt+Tab` | Switch to the next window |
| `Ctrl+W` | Close the focused window (asks before losing unsaved changes) |
| `Ctrl+Q` | Quit Fenrir |
| `F5` / `F6` | Move / resize the focused window; use the arrow keys, then `Enter` or `Esc` |
| `F7` | Toggle maximize/restore for the focused window |
| `F8` | Minimize the focused window |
| Arrow keys | Move the desktop cursor or navigate the focused widget |
| `Enter` / `Space` | Activate the item under the desktop cursor or the selected widget |
| `Esc` | Leave the current interaction mode or close a menu/dialog |

Fenrir routes keys according to the current mode. In a focused text-editing
widget, printable characters go directly to that widget. `Esc` steps out of
editing/navigation modes; the Terminal app forwards it to the shell instead.

### Text Editor

| Key | Action |
| --- | --- |
| `Ctrl+N` | New tab |
| `Ctrl+O` | Open a file in a tab |
| `Ctrl+D` | Open a folder in the file tree |
| `Ctrl+B` | Toggle focus between the file tree and text |
| `Ctrl+S` | Save |
| `Ctrl+T` | Close the current tab |
| `Ctrl+E` | Toggle line wrapping |
| `Ctrl+G` | Toggle line numbers |
| `Ctrl+Y` | Toggle spaces instead of tab characters |
| `Alt+PgUp` / `Alt+PgDn` | Previous / next tab |

### File Manager

| Key | Action |
| --- | --- |
| `Tab`, `Left`, `Right` | Switch panels |
| `Backspace` | Go to the parent directory |
| `Ctrl+C` / `Ctrl+X` | Copy / move the selection to the other panel |
| `Ctrl+R` | Rename the selection |
| `Ctrl+G` | Choose a folder for the active panel |
| `Delete` | Delete the selection |

### Calculator

Use the number and operator keys, or move between buttons with the arrow keys
and press `Enter` or `Space`. `Ctrl+C` clears the calculation.

### Calendar

| Key | Action |
| --- | --- |
| `Ctrl+N` | Create an event |
| `Ctrl+T` / `Ctrl+D` | Week / day view |
| `Ctrl+Up` / `Ctrl+Down` | Previous / next year |
| `Backspace` | Return from week/day view to the month |
| `Enter` / `Delete` | Open or edit / delete the selected event |

### Paint

Arrow keys move the drawing cursor; printable characters paint, and `Delete`
erases. `F3` opens the color palette. `Ctrl+N`, `Ctrl+O`, `Ctrl+S`, and
`Ctrl+Z` create, open, save, and undo.

### Terminal

The Terminal app starts `$SHELL` (or `/bin/sh`) in a pseudoterminal. Keys are
forwarded to the shell; `Alt+Tab`, `F2`, `F8`, and `Ctrl+W` remain Fenrir window
controls. `Esc` is sent to the shell.

## Project layout

- `include/fenrir/` — framework interfaces, core types, terminal I/O, renderer,
  and reusable widgets.
- `src/` — framework and platform-facing implementations.
- `apps/` — built-in apps and the shell entry point (`apps/shell/main.cpp`).
- `CMakeLists.txt` — builds the `fenrir` executable and its static framework
  library.

New apps implement `fenrir::App`, provide a `Widget` view, and register in
`apps/shell/main.cpp`. Prefer extending the shared widgets and key-routing
conventions so features remain consistent across apps.
