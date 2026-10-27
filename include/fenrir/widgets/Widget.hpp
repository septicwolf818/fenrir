#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/Screen.hpp"
#include "fenrir/input/Key.hpp"

namespace fenrir {

// A single menu item (submenu). Associates a label with an action.
struct MenuItem {
    std::u32string label;      // Text shown in the menu.
    std::function<void()> cb;   // Action invoked when selected.
};

// A group of items — a top-level menu tab (e.g. "File", "Edit").
using MenuTab = std::pair<std::u32string, std::vector<MenuItem>>;

// An application keyboard shortcut, consisting of a single key press.
struct KeyShortcut {
    bool ctrl = false;
    Key key = Key::Unknown;
    std::optional<char32_t> character = std::nullopt; // Optional character (e.g. U'C' for Ctrl+C).
    bool alt = false; // Alt is the only modifier distinguishable on a bare TTY for
                      // compound keys (Ctrl+PgUp produces the same bytes as PgUp).
};

// Result returned by handleKey — Desktop uses it to determine the current mode.
enum class KeyEventResult {
    Ignored,      // No child handled this key.
    Consumed,     // Handled without a mode change (e.g. container navigation).
    EnteredEdit,  // Editing started — enter Edit mode for this widget.
    ExitedEdit,   // Editing ended — return to Keyboard mode for this widget.
    Activated,    // A child was activated (e.g. a button via Enter/Space).
};

// Base class for all widgets. A widget knows only its rectangle
// (coordinates RELATIVE TO the screen, not the window — Window converts them
// in setBounds so the widget itself does not need to know about the window).
class Widget {
public:
    virtual ~Widget() = default;

    void setBounds(const Rect& r) { bounds_ = r; }
    const Rect& bounds() const { return bounds_; }

    // focused_ indicates the active content target within this window. A window
    // retains its focused content styling while inactive; its frame indicates
    // global window focus.
    // highlighted_ indicates whether this widget is currently highlighted (e.g. selected during navigation).
    void setFocused(bool f) { focused_ = f; }
    bool focused() const { return focused_; }
    void setHighlighted(bool h) { highlighted_ = h; }
    bool highlighted() const { return highlighted_; }

    // Enabled/disabled (inactive) status. A disabled widget is drawn dimmed
    // and is skipped by container navigation (selectable() is false) and
    // never enters Edit mode (editable() is false) — e.g. settings rows
    // that only apply while their feature is switched on. Defaults to enabled.
    void setEnabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    // Does this widget accept text directly (is it editable)? true means pressing a character
    // enters Edit mode for this widget. Defaults to false; only widgets with actual editing
    // (e.g. TextArea and text fields) set it. Containers (Frame) inherit their child's editability.
    virtual bool editable() const { return enabled_ && editable_; }
    void setEditable(bool e) { editable_ = e; }

    // Does this widget contain unsaved changes? Desktop asks for confirmation before
    // closing a window with modified content (close guard; see closeWithConfirm in
    // Desktop.cpp). Defaults to false; widgets with actual editing state override it
    // (TextArea uses its own dirty(), while app views — TextEditor and Paint — delegate).
    // Dialogs and file pickers are always clean, so they do not need an override.
    virtual bool dirty() const { return false; }

    // Click in the window content using the virtual cursor (Enter/Space in Desktop mode).
    // A widget can handle a hit in its own area (e.g. an editor tab) and return true;
    // by default, false means Desktop performs the standard window activation
    // (entering Keyboard mode).
    virtual bool clickAt(int /*x*/, int /*y*/) { return false; }

    // --- Menu / shortcuts API (used by Desktop for rendering and shortcuts) ---

    // All menu tabs for this app (set by createView() in Widget).
    const std::vector<MenuTab>& menuTabs() const { return menuTabs_; }
    void setMenuTabs(std::vector<MenuTab> tabs) { menuTabs_ = std::move(tabs); }
    void pushMenuTab(MenuTab tab) { menuTabs_.push_back(std::move(tab)); }
    // Appends an item to a tab (e.g. Exit to File); a missing tab is created
    // FIRST so File stays ahead of the other tabs.
    void pushMenuItem(std::u32string tabName, MenuItem item) {
        for (auto& tab : menuTabs_) {
            if (tab.first == tabName) {
                tab.second.push_back(std::move(item));
                return;
            }
        }
        menuTabs_.insert(menuTabs_.begin(), {std::move(tabName), {std::move(item)}});
    }

    // Keyboard shortcuts for this app.
    const std::vector<std::pair<KeyShortcut, std::function<void()>>>& shortcuts() const { return shortcuts_; }
    void setShortcuts(std::vector<std::pair<KeyShortcut, std::function<void()>>> s) { shortcuts_ = std::move(s); }

    // Add a single shortcut (used by createView()).
    void pushShortcut(KeyShortcut sc, std::function<void()> cb) {
        shortcuts_.push_back({std::move(sc), std::move(cb)});
    }

    virtual void draw(Screen& screen) = 0;

    // Return KeyEventResult — Desktop uses it to determine the current mode.
    virtual KeyEventResult handleKey(const KeyEvent& ev) = 0;

    // Activate via Enter/Space (called by containers such as Panel instead of
    // dynamic_cast to a specific widget type). true means the widget handled activation.
    virtual bool activate() { return false; }

    // Can this widget be selected during container navigation? Panel skips
    // elements where selectable() == false, such as labels. A disabled
    // widget is never selectable. Subclasses with their own conditions
    // (e.g. LineEdit's read-only) AND them with enabled().
    virtual bool selectable() const { return enabled_; }

    // Absolute GUI cursor position (in screen cells), if the widget has an editing
    // context (e.g. TextArea). std::nullopt means the widget has no cursor of its own.
    virtual std::optional<std::pair<int, int>> cursorPosition() const { return std::nullopt; }

    // Whether the desktop should draw its blinking cursor over this widget.
    // Terminal-like widgets can hide it when the child process hides its cursor.
    virtual bool wantsGuiCursor() const { return true; }

    // Minimum CONTENT size (screen cells) required for the widget to be usable
    // (e.g. a calendar needs a 30x10 grid). The window adds its frame/title bar
    // and enforces this minimum during resizeBy (F6) and creation, so content is
    // never made smaller than the minimum. Defaults to 0 (no minimum).
    virtual int minWidth() const { return 0; }
    virtual int minHeight() const { return 0; }

    // Widget lifetime token for safe ASYNCHRONOUS callbacks (dialogs opened by
    // AppContext). A callback captures a weak_ptr: if the window containing this
    // widget closes before the dialog returns, lock() returns nullopt instead of
    // causing undefined behavior through a dangling `this`:
    //     auto token = lifetimeToken();
    //     ctx->confirm(msg, [token, this](bool ok){ if (!token.lock()) return; ... });
    std::weak_ptr<int> lifetimeToken() const { return lifetime_; }

protected:
    Rect bounds_{};
    bool focused_ = false;
    bool highlighted_ = false;
    bool editable_ = false;
    bool enabled_ = true;

    // App metadata — set by createView() in Widget.
    std::vector<MenuTab> menuTabs_;
    std::vector<std::pair<KeyShortcut, std::function<void()>>> shortcuts_;

private:
    std::shared_ptr<int> lifetime_ = std::make_shared<int>(0);
};

} // namespace fenrir
