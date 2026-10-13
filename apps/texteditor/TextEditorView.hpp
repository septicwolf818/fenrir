#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Prefs.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/widgets/FileTree.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/TabBar.hpp"
#include "fenrir/widgets/TextArea.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Editor view: tab bar (TabBar) + TextArea + StatusBar in a single window.
// Every open tab = its own TextArea (separate buffer, cursor, path).
// Ctrl+N = new tab (nothing is lost — no guard needed, it's a fresh buffer),
// Ctrl+O = open in a new tab (already open = switches), Ctrl+T =
// close tab (dirty asks for confirmation), Alt+PgDn/PgUp = next/previous.
// dirty() = ANY tab is dirty — the window-close guard protects everything.
// Settings (line numbers, spaces instead of tabs) are shared by all tabs, stored
// permanently in prefs::forApp("editor").
class TextEditorView : public Widget {
public:
    TextEditorView(AppContext& ctx, std::string startPath) : ctx_(ctx) {
        auto& p = prefs::forApp("editor");
        lineNumbers_ = p.getBool("line_numbers", false);
        insertsSpaces_ = p.getBool("insert_spaces", false);
        openNewTab(std::move(startPath));
        refreshTabBar();
        // Enter in the tree opens the file in a new tab (already open = switch)
        // and moves focus to the text; Ctrl+B returns to the tree.
        tree_.onOpen = [this](const std::string& path) {
            openInEditor(path);
            treeFocused_ = false;
            status_.setMessage(text::toUtf32("Opened: " + fs::nameOf(path)));
        };
    }

    // --- Tabs ----------------------------------------------------------------

    void newFile() { openNewTab(""); }

    void openFile() {
        std::string startDir = cur().path().empty() ? fs::homeDir() : fs::parent(cur().path());
        const auto token = lifetimeToken();
        ctx_.openFile(startDir, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            openInEditor(picked);
        });
    }

    void closeTab() {
        auto close = [this] {
            tabs_.erase(tabs_.begin() + current_);
            if (tabs_.empty()) openNewTab("");
            if (current_ >= static_cast<int>(tabs_.size()))
                current_ = static_cast<int>(tabs_.size()) - 1;
            refreshTabBar();
            status_.setMessage(U"Tab closed");
        };
        if (!cur().dirty()) { close(); return; }
        const auto token = lifetimeToken();
        ctx_.confirm(U"Unsaved changes. Close the tab?",
                     [token, close = std::move(close)](bool ok) {
                         if (!token.lock()) return;
                         if (ok) close();
                     });
    }

    void nextTab() { switchTo(current_ + 1); }
    void prevTab() { switchTo(current_ - 1); }

    // --- Folder / file tree ------------------------------------------------

    // Ctrl+D: opens the folder picker dialog; once picked, the tree panel appears on
    // the left (FileTree), root selected, focus in the tree.
    void openFolder() {
        std::string startDir = cur().path().empty() ? fs::homeDir() : fs::parent(cur().path());
        const auto token = lifetimeToken();
        ctx_.openFolder(startDir, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            tree_.setRoot(picked);
            treeOpen_ = true;
            treeFocused_ = treeWidth() > 0;
            status_.setMessage(text::toUtf32("Folder: " + picked));
        });
    }

    // Ctrl+B: toggles focus tree <-> text; if the tree isn't open —
    // behaves like Ctrl+D (dialog right away).
    void toggleTreeFocus() {
        if (!treeOpen_) {
            openFolder();
            return;
        }
        treeFocused_ = !treeFocused_ && treeWidth() > 0;
        status_.setMessage(treeFocused_ ? U"File panel" : U"Text editing");
    }

    // --- File operations (current tab) ------------------------------------

    void save() {
        if (cur().path().empty()) { saveAs(); return; }
        cur().saveToCurrentPath();
        refreshTabBar(); // after saving a new file the label changes
    }

    void saveAs() {
        // Suggest the CURRENT file's name (documentN.txt), not the fixed
        // "document.txt" — otherwise "Save as" would suggest the wrong name.
        std::string startDir = cur().path().empty() ? fs::homeDir() : fs::parent(cur().path());
        std::string suggest =
            cur().path().empty() ? std::string("document.txt") : fs::nameOf(cur().path());
        const auto token = lifetimeToken();
        ctx_.saveFile(startDir, suggest, [this, token](const std::string& picked) {
            if (!token.lock()) return;
            cur().saveFile(picked);
            refreshTabBar();
        });
    }

    // --- Format / preferences --------------------------------------------------

    void toggleWrap() { cur().toggleWrap(); }

    // Line numbers (Ctrl+G) — the gutter; stored in the editor preferences.
    void toggleLineNumbers() {
        lineNumbers_ = !lineNumbers_;
        applyToAll([](TextArea& a, bool ln, bool) { a.setShowLineNumbers(ln); });
        prefs::forApp("editor").setBool("line_numbers", lineNumbers_);
        status_.setMessage(lineNumbers_ ? U"Line numbers: on" : U"Line numbers: off");
    }

    // Tab inserts spaces instead of a tab character (Ctrl+Y); stored in the preferences.
    void toggleSpaces() {
        insertsSpaces_ = !insertsSpaces_;
        applyToAll([](TextArea& a, bool, bool sp) { a.setInsertsSpaces(sp); });
        prefs::forApp("editor").setBool("insert_spaces", insertsSpaces_);
        status_.setMessage(insertsSpaces_ ? U"Tab = spaces (4): on" : U"Tab = tab: spaces off");
    }

    // --- Widget ----------------------------------------------------------------

    void draw(Screen& screen) override {
        // TextArea dirty state changes while typing; update tab markers only
        // only when the active tab's state changes instead of rebuilding every frame.
        if (currentDirtyShown_ != cur().dirty()) {
            currentDirtyShown_ = cur().dirty();
            tabBar_.setDirty(current_, currentDirtyShown_);
        }

        const int statusH = 1, tabH = 1;
        const int bodyH = std::max(0, bounds_.h - statusH);
        const int areaH = std::max(0, bodyH - tabH);

        // Tree on the left (when open); right column: tabs + text.
        const int treeW = treeWidth();
        if (treeOpen_ && treeW > 0) {
            tree_.setBounds(Rect{bounds_.x, bounds_.y, treeW, bodyH});
            tree_.setFocused(focused_ && treeFocused_);
            tree_.draw(screen);
        }

        const int rx = bounds_.x + treeW;
        const int rw = bounds_.w - treeW;
        tabBar_.setBounds(Rect{rx, bounds_.y, rw, tabH});
        tabBar_.draw(screen);

        cur().setBounds(Rect{rx, bounds_.y + tabH, rw, areaH});
        cur().setFocused(focused_ && !treeFocused_);
        cur().draw(screen);

        auto [row, col] = cur().cursorLineCol();
        std::u32string left = text::toUtf32(std::string("File: ") +
                                           (cur().path().empty() ? std::string("(untitled)") : cur().path())) +
                              U" [" + text::toUtf32(std::to_string(current_ + 1)) + U"/" +
                              text::toUtf32(std::to_string(tabs_.size())) + U"]";
        std::u32string right = text::toUtf32(std::to_string(row) + ":" + std::to_string(col));
        if (cur().dirty()) right += U" *";

        status_.setBounds(Rect{bounds_.x, bounds_.y + bodyH, bounds_.w, statusH});
        status_.setText(std::move(left), std::move(right));
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (treeOpen_ && treeFocused_ && treeWidth() > 0) {
            // Tab from the tree returns to the text (Alt+Tab too — but it is
            // intercepted by Desktop, not the tree). Esc deliberately NOT — Desktop
            // reserves it for switching cursor modes (Edit -> Keyboard -> Desktop).
            if (ev.key == Key::Tab && !ev.ctrl && !ev.alt) {
                treeFocused_ = false;
                return KeyEventResult::Consumed;
            }
            return tree_.handleKey(ev);
        }
        if (treeFocused_) treeFocused_ = false; // The panel may have been hidden by a resize.
        return cur().handleKey(ev);
    }

    bool editable() const override { return cur().editable(); }

    // Unsaved changes — Desktop asks before closing the window. Any tab being
    // dirty triggers it (closing the window loses all of them — we guard everything).
    bool dirty() const override {
        for (const auto& t : tabs_)
            if (t->dirty()) return true;
        return false;
    }

    // Virtual cursor click (Enter/Space in Desktop mode):
    // - tree panel: focus the tree and enter the window (false = Desktop
    //   switches to window mode; keys reach the tree because of treeFocused_),
    // - tab bar: switch to the tab under the cursor (true),
    // - remaining content (text): focus the text, standard entry (false).
    bool clickAt(int x, int y) override {
        const int treeW = treeWidth();
        const int bodyH = std::max(0, bounds_.h - 1); // without the status bar
        if (treeOpen_ && treeW > 0 &&
            x >= bounds_.x && x < bounds_.x + treeW &&
            y >= bounds_.y && y < bounds_.y + bodyH) {
            treeFocused_ = true;
            tree_.clickAt(x, y);
            return false; // enter the window with focus in the tree
        }
        treeFocused_ = false; // click in the content = focus in the text
        if (tabBar_.count() == 0 || y != tabBar_.bounds().y) return false;
        if (tabBar_.scrollHit(x)) return true; // <> buttons — scroll without changing tabs
        const int i = tabBar_.tabAt(x);
        if (i < 0) return false;
        if (i != current_) switchTo(i);
        return true;
    }

    std::optional<std::pair<int, int>> cursorPosition() const override {
        if (treeOpen_ && treeFocused_) return std::nullopt; // GUI cursor in the tree? no
        return cur().cursorPosition();
    }

    // Minimum: a tab + a few lines of text + the status bar.
    int minWidth() const override { return 10; }
    int minHeight() const override { return 4; }

private:
    TextArea& cur() { return *tabs_[static_cast<std::size_t>(current_)]; }
    const TextArea& cur() const { return *tabs_[static_cast<std::size_t>(current_)]; }

    // Tree panel width — the same geometry in draw() and clickAt().
    int treeWidth() const {
        if (!treeOpen_) return 0;
        int treeW = std::clamp(bounds_.w / 3, 12, 28);
        if (bounds_.w - treeW < 20) treeW = std::max(0, bounds_.w - 20);
        return treeW;
    }

    void openNewTab(const std::string& path) {
        auto area = std::make_unique<TextArea>();
        area->onStatus = [this](const std::string& m) { status_.setMessage(text::toUtf32(m)); };
        area->setShowLineNumbers(lineNumbers_);
        area->setInsertsSpaces(insertsSpaces_);
        if (!path.empty()) area->loadFile(path);
        tabs_.push_back(std::move(area));
        current_ = static_cast<int>(tabs_.size()) - 1;
        refreshTabBar();
    }

    void openInEditor(const std::string& path) {
        // Already open? Switch instead of duplicating the tab.
        for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
            if (tabs_[static_cast<std::size_t>(i)]->path() == path) {
                switchTo(i);
                return;
            }
        }
        openNewTab(path);
    }

    void switchTo(int idx) {
        if (tabs_.empty()) return;
        const int n = static_cast<int>(tabs_.size());
        current_ = ((idx % n) + n) % n;
        refreshTabBar(); // also refreshes the bar selection and dirty markers
    }

    void refreshTabBar() {
        std::vector<TabBar::Tab> tabs;
        tabs.reserve(tabs_.size());
        int unnamed = 0;
        for (const auto& area : tabs_) {
            const std::string& p = area->path();
            std::u32string label = p.empty()
                ? U"(untitled " + text::toUtf32(std::to_string(++unnamed)) + U")"
                : text::toUtf32(fs::nameOf(p));
            tabs.push_back(TabBar::Tab{std::move(label), area->dirty()});
        }
        tabBar_.setTabs(std::move(tabs));
        tabBar_.select(current_);
        currentDirtyShown_ = cur().dirty();
    }

    template <typename F>
    void applyToAll(F&& f) {
        for (auto& t : tabs_) f(*t, lineNumbers_, insertsSpaces_);
    }

    AppContext& ctx_;
    std::vector<std::unique_ptr<TextArea>> tabs_;
    int current_ = 0;
    TabBar tabBar_;
    FileTree tree_;
    bool treeOpen_ = false;
    bool treeFocused_ = false;
    StatusBar status_;
    bool lineNumbers_ = false;
    bool insertsSpaces_ = false;
    bool currentDirtyShown_ = false;
};

} // namespace fenrir::apps
