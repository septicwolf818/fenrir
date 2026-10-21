#pragma once
#include <functional>
#include <string>

namespace fenrir::clipboard {

// The workspace clipboard — text copied in one application can be pasted in
// another one.
//
// A text console has no system clipboard, so the text is kept in a file
// (~/.config/fenrir/clipboard.txt) that every Fenrir process shares. The
// Desktop additionally pushes the copied text to the terminal with OSC 52:
// graphical terminals that support it then hold the text in the desktop
// clipboard, so it can be pasted outside of Fenrir as well.
//
// The clipboard holds UTF-8 text; newlines are allowed.

void setText(const std::string& utf8);

// The current clipboard content ("" when the clipboard is empty).
std::string text();

// True when the clipboard holds nothing.
bool empty();

void clear();

// Sink for the copied text (UTF-8). The Desktop installs a sink that writes
// the OSC 52 sequence to the terminal; other owners may install their own.
void setTerminalSink(std::function<void(const std::string& utf8)> sink);

// Base64 (used for the OSC 52 clipboard sequence).
std::string base64Encode(const std::string& data);

} // namespace fenrir::clipboard
