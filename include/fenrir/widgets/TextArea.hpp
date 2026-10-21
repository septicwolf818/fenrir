#pragma once
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "fenrir/core/FileWatchdog.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

enum class SyntaxToken : uint8_t { Identifier, Keyword, Type, String, Number, Comment, Directive };

struct SyntaxSpan {
    std::size_t begin = 0;
    std::size_t end = 0; // exclusive
    SyntaxToken token = SyntaxToken::Keyword;
};

// Multiline text editor with word wrapping. The buffer stores LOGICAL lines
// (std::u32string, full Unicode); layout converts them to VISUAL ROWS of width
// bounds_.w, wrapping at the last word that fits and hard-wrapping words wider
// than the screen. The cursor and scrolling operate on visual rows, so long
// lines remain fully visible and editable.
class TextArea : public Widget {
public:
    TextArea();

    void loadFile(const std::string& path);
    void saveFile(const std::string& path);
    void saveToCurrentPath(); // Save to the path used for the last load or save.

    // Optional line lexer. It receives a logical line plus whether the line
    // STARTS inside a block comment continued from above (see BlockCommentScan),
    // and returns token spans; plain-text buffers leave this unset.
    // NOTE: draw() lexes only the visible rows (starting mid-buffer when
    // scrolled, and wrapped lines repeatedly), so the lexer must be stateless
    // across calls — all multiline state comes through startsInBlockComment.
    using SyntaxHighlighter =
        std::function<void(std::u32string_view, bool, std::vector<SyntaxSpan>&)>;
    void setSyntaxHighlighter(SyntaxHighlighter highlighter) {
        syntaxHighlighter_ = std::move(highlighter);
        blockValid_ = false;
    }

    // Whole-buffer block-comment scan: result[i] is nonzero when logical line
    // i STARTS inside a block comment (e.g. /* ... */). Implemented by the
    // language module — it must mirror the line lexer's string/comment rules.
    // Empty function = the language has no block comments (no cache is kept).
    // TextArea caches the result and recomputes it only after an edit, so
    // drawing stays cheap: one linear scan per keystroke, not per frame.
    using BlockCommentScan =
        std::function<std::vector<char>(const std::vector<std::u32string>&)>;
    void setBlockCommentScan(BlockCommentScan scan) {
        blockScan_ = std::move(scan);
        blockValid_ = false;
    }

    void newFile();   // Create a new empty document.
    void clearText(); // Clear all text.

    // Tab insertion: a tab character ('\t' by default) or spaces (up to the next
    // multiple-of-4 column). Set from the editor's application preferences;
    // in either mode, Tab IS inserted (it does not switch windows).
    void setInsertsSpaces(bool on) { insertsSpaces_ = on; }
    bool insertsSpaces() const { return insertsSpaces_; }

    // Line numbers (left gutter), toggled from the menu or a shortcut.
    // --- Selection -----------------------------------------------------------
    // A selection is an anchor (logical line/column) plus the cursor as the
    // other end. Shift+Arrow extends it, Ctrl+C/X/V work with the global
    // clipboard (see fenrir/core/Clipboard.hpp).
    bool hasSelection() const { return selecting_; }
    // The selected text ("" when nothing is selected).
    std::u32string selectedText() const;
    void selectAll();
    void clearSelection();
    // Copies the selection to the clipboard; true when something was copied.
    bool copySelection();
    // Copies the selection and removes it from the buffer.
    bool cutSelection();
    // Inserts the clipboard content at the cursor (replacing the selection).
    void pasteClipboard();

    void setShowLineNumbers(bool on) {
        if (lineNumbers_ == on) return;
        lineNumbers_ = on;
        markDirty();
    }
    bool showLineNumbers() const { return lineNumbers_; }

    const std::string& path() const { return path_; }
    bool dirty() const override { return dirty_; }

    // --- External changes ---------------------------------------------------
    // The file this buffer came from can be changed or removed by another
    // program after it was loaded. The stamp below is the state of the file
    // when it was loaded or saved; the editor compares it with the current
    // state on disk (FileWatchdog::stateOf) to notice such a change.
    const FileWatchdog::State& fileStamp() const { return fileStamp_; }
    // When the file was loaded or saved by the editor (0 = unknown).
    std::time_t fileSavedAt() const { return fileSavedAt_; }

    // Cursor position (line, column), both 1-based — for the status bar.
    std::pair<int, int> cursorLineCol() const { return {cursorRow_ + 1, cursorCol_ + 1}; }

    // Moves the cursor to a 1-based (line, column) position, clamped to the
    // buffer. Used to keep the place after the file was reloaded from disk.
    void setCursorLineCol(int line, int col);

    // Word wrapping is enabled by default. When disabled, long lines extend
    // beyond the screen and scroll HORIZONTALLY (scrollCol_, as in LineEdit),
    // keeping the cursor and all content accessible even in a narrow window.
    bool wrap() const { return wrap_; }
    void setWrap(bool on) {
        if (wrap_ == on) return;
        wrap_ = on;
        scrollCol_ = 0;
        markDirty();
        ensureVisible();
    }
    void toggleWrap() {
        setWrap(!wrap_);
        if (onStatus) onStatus(wrap_ ? "Line wrapping: enabled" : "Line wrapping: disabled - horizontal scrolling");
    }

    std::function<void(const std::string& message)> onStatus;
    std::function<void()> onSaveRequested; // Ctrl+S
    std::function<void()> onOpenRequested; // Ctrl+O

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& ev) override;
    std::optional<std::pair<int, int>> cursorPosition() const override;

private:
    // One visual row: a segment of logical line `line`, starting at column `col0`
    // and having length `len`. Segments are contiguous — each line character
    // appears exactly once in the layout (a wrapping space stays at the end of
    // the row), so mapping between logical and visual columns is straightforward.
    struct Seg {
        int line;
        int col0;
        int len;
    };

    std::vector<std::u32string> lines_{std::u32string()};
    mutable std::vector<Seg> layout_;
    // Mutable layout state: cursorPosition() is const, and the layout is computed
    // lazily (including when reading the cursor position).
    mutable int layoutW_ = 0; // Width used to build the layout.
    mutable bool layoutDirty_ = true;

    std::string path_;
    bool dirty_ = false;
    // Selection anchor (logical line/column); the cursor is the other end.
    int selRow_ = 0, selCol_ = 0;
    bool selecting_ = false;
    // State of the file on disk when it was loaded or saved (see fileStamp()).
    FileWatchdog::State fileStamp_;
    std::time_t fileSavedAt_ = 0;
    SyntaxHighlighter syntaxHighlighter_;
    BlockCommentScan blockScan_;
    mutable std::vector<char> blockStart_; // per-line "starts in block comment"
    mutable bool blockValid_ = false;

    int cursorRow_ = 0, cursorCol_ = 0; // Cursor position in the LOGICAL LINE.
    int scrollRow_ = 0;                 // First visible VISUAL ROW.
    int scrollCol_ = 0;                 // First visible column (only when wrapping is disabled).
    bool wrap_ = true;                  // Word wrapping (enabled by default).
    bool insertsSpaces_ = false;        // Tab inserts spaces instead of '\t'.
    bool lineNumbers_ = false;          // Line-number gutter.
    int goalCol_ = 0; // "Sticky" visual column for vertical movement (PageUp, etc.).

    // Every buffer mutator calls markDirty — piggybacking the block-comment
    // invalidation here (instead of 10 separate sites) keeps the two caches
    // from drifting apart. Non-mutating callers (wrap, line numbers) just
    // pay one cheap linear rescan on the next draw.
    void markDirty() {
        layoutDirty_ = true;
        blockValid_ = false;
    }
    void ensureBlockStates() const; // recompute blockStart_ unless valid
    void ensureLayout() const;
    void rebuildLayout() const;
    int gutterW() const;               // Width of the line-number gutter (0 = none).
    int visualRowOf(int row, int col) const; // Index in layout_ for (row, col).
    std::pair<int, int> logicalOf(int segIdx, int offset) const;
    int visualOffset() const; // Current cursor offset within its visual row.
    void syncGoal();          // After horizontal movement, set goal to the current visual offset.

    void insertChar(char32_t ch);
    void insertNewline();
    // Inserts a piece of text (may contain newlines) at the cursor.
    void insertText(const std::u32string& text);
    // --- Selection helpers (private) -----------------------------------------
    // Normalized selection range: start line/column and end line/column, the
    // end being exclusive.
    std::tuple<int, int, int, int> selectionRange() const;
    bool isSelected(int line, int col) const;
    void beginSelection();   // anchors the selection at the cursor
    void deleteSelection();  // removes the selected text
    void backspace();
    void del();
    void moveHoriz(int dCol); // Move left/right, crossing between lines.
    void moveVert(int dSeg);  // Move up/down by visual row(s) (sticky goal).
    void clampCursor();
    void ensureVisible();
};

} // namespace fenrir
