#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

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

    void newFile();   // Create a new empty document.
    void clearText(); // Clear all text.

    // Tab insertion: a tab character ('\t' by default) or spaces (up to the next
    // multiple-of-4 column). Set from the editor's application preferences;
    // in either mode, Tab IS inserted (it does not switch windows).
    void setInsertsSpaces(bool on) { insertsSpaces_ = on; }
    bool insertsSpaces() const { return insertsSpaces_; }

    // Line numbers (left gutter), toggled from the menu or a shortcut.
    void setShowLineNumbers(bool on) {
        if (lineNumbers_ == on) return;
        lineNumbers_ = on;
        markDirty();
    }
    bool showLineNumbers() const { return lineNumbers_; }

    const std::string& path() const { return path_; }
    bool dirty() const override { return dirty_; }

    // Cursor position (line, column), both 1-based — for the status bar.
    std::pair<int, int> cursorLineCol() const { return {cursorRow_ + 1, cursorCol_ + 1}; }

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

    int cursorRow_ = 0, cursorCol_ = 0; // Cursor position in the LOGICAL LINE.
    int scrollRow_ = 0;                 // First visible VISUAL ROW.
    int scrollCol_ = 0;                 // First visible column (only when wrapping is disabled).
    bool wrap_ = true;                  // Word wrapping (enabled by default).
    bool insertsSpaces_ = false;        // Tab inserts spaces instead of '\t'.
    bool lineNumbers_ = false;          // Line-number gutter.
    int goalCol_ = 0; // "Sticky" visual column for vertical movement (PageUp, etc.).

    void markDirty() { layoutDirty_ = true; }
    void ensureLayout() const;
    void rebuildLayout() const;
    int gutterW() const;               // Width of the line-number gutter (0 = none).
    int visualRowOf(int row, int col) const; // Index in layout_ for (row, col).
    std::pair<int, int> logicalOf(int segIdx, int offset) const;
    int visualOffset() const; // Current cursor offset within its visual row.
    void syncGoal();          // After horizontal movement, set goal to the current visual offset.

    void insertChar(char32_t ch);
    void insertNewline();
    void backspace();
    void del();
    void moveHoriz(int dCol); // Move left/right, crossing between lines.
    void moveVert(int dSeg);  // Move up/down by visual row(s) (sticky goal).
    void clampCursor();
    void ensureVisible();
};

} // namespace fenrir
