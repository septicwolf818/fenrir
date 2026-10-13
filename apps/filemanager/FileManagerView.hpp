#pragma once
#include <filesystem>
#include <optional>
#include <string>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/widgets/DirectoryView.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir::apps {

// Main view of the File Manager: TWO panels (each a DirectoryView from the
// widget library) + status bar. No custom list drawing — everything goes
// through reused widgets (DirectoryView, StatusBar). User prompts
// (delete/rename/go to) go through AppContext (Dialog/LineEdit from the library).
//
// Copy model as in classic dual-panel file managers: Ctrl+C copies
// the selection from the active panel TO THE OTHER panel, Ctrl+X moves it.
// No clipboard and no pasting — the target panel is always known.
class FileManagerView : public Widget {
public:
    FileManagerView(AppContext& ctx, std::string startDir)
        : ctx_(ctx), left_(std::move(startDir)), right_(".") { // right panel starts in the current directory (cwd)
        auto info = [this](const std::string& path) {
            long long sz = fs::fileSize(path);
            status("File: " + path + (sz >= 0 ? " (" + std::to_string(sz) + " B)" : ""));
        };
        left_.onFileActivate = info;
        right_.onFileActivate = info;
        left_.onStatus = [this](const std::string& m) { status(m); };
        right_.onStatus = [this](const std::string& m) { status(m); };
    }

    // --- Operations also invoked from the app menu ---------------------------

    void goUp() {
        const std::string before = active().currentDir();
        active().goUp();
        // At the root, parent() returns itself and the panel does not move — the
        // message must not pretend we went up. Spelling matches the "Up" menu item.
        if (active().currentDir() == before) status("Already at the root.");
        else status("Up: " + active().currentDir());
    }

    void copySelected() {
        DirectoryView& src = active();
        if (src.empty()) { status("No items."); return; }
        if (!src.selectionIsSafeTarget()) { status("Cannot copy '..'."); return; }
        DirectoryView& dest = other();
        const std::string from = src.selectedPath();
        // Target directory = source or its descendant -> stdfs::copy would try to
        // copy a directory into itself (recursion until the disk fills up).
        if (fs::isDirectory(from) && isWithin(dest.currentDir(), from)) {
            status("Target is the source directory or its subdirectory.");
            return;
        }
        try {
            const std::string to = uniqueNameIn(dest.currentDir(), fs::nameOf(from));
            fs::copyPath(from, to);
            reloadBoth();
            status("Copied: " + fs::nameOf(from) + " -> " + dest.currentDir());
        } catch (const std::exception& e) {
            status(std::string("Error: ") + e.what());
        }
    }

    void moveSelected() {
        DirectoryView& src = active();
        if (src.empty()) { status("No items."); return; }
        if (!src.selectionIsSafeTarget()) { status("Cannot move '..'."); return; }
        DirectoryView& dest = other();
        // Both panels in the same directory: uniqueNameIn would produce a new name and
        // the "move" would end as a silent rename to "name (1)".
        if (sameDirectory(dest.currentDir(), src.currentDir())) {
            status("Source and target are in the same directory.");
            return;
        }
        const std::string from = src.selectedPath();
        // Target = source or its descendant: rename into one's own descendant always
        // fails, and the relative fallback (copy + delete) would try to copy a
        // directory into itself.
        if (fs::isDirectory(from) && isWithin(dest.currentDir(), from)) {
            status("Target is the source directory or its subdirectory.");
            return;
        }
        try {
            const std::string to = uniqueNameIn(dest.currentDir(), fs::nameOf(from));
            try {
                fs::renamePath(from, to);
            } catch (const std::exception&) {
                // Different filesystems (e.g. EXDEV) — rename doesn't work:
                // copy, then delete the source (sequence as in old file managers).
                fs::copyPath(from, to);
                fs::removePath(from);
            }
            reloadBoth();
            status("Moved: " + fs::nameOf(from) + " -> " + dest.currentDir());
        } catch (const std::exception& e) {
            status(std::string("Error: ") + e.what());
        }
    }

    void renameSelected() {
        DirectoryView& v = active();
        if (v.empty()) { status("No items."); return; }
        if (!v.selectionIsSafeTarget()) { status("Cannot rename '..'."); return; }
        const std::string oldPath = v.selectedPath();
        const auto token = lifetimeToken();
        ctx_.prompt(U"Rename", U"New name:", U"Name: ",
                    text::toUtf32(fs::nameOf(oldPath)),
                    [this, token, oldPath](std::optional<std::u32string> newName) {
                        if (!token.lock()) return; // the window may have been closed
                        if (!newName || newName->empty()) return;
                        const std::string newBase = text::toUtf8(*newName);
                        if (newBase == "." || newBase == ".." ||
                            newBase.find('/') != std::string::npos) {
                            status("Enter a single file or folder name.");
                            return;
                        }
                        const std::string newPath = fs::join(fs::parent(oldPath), newBase);
                        try {
                            if (fs::exists(newPath) && newPath != oldPath) {
                                status("Already exists: " + newBase);
                                return;
                            }
                            fs::renamePath(oldPath, newPath);
                            reloadBoth();
                            status("Renamed to: " + newBase);
                        } catch (const std::exception& e) {
                            status(std::string("Error: ") + e.what());
                        }
                    });
    }

    void deleteSelected() {
        DirectoryView& v = active();
        if (v.empty()) { status("No items."); return; }
        if (!v.selectionIsSafeTarget()) { status("Cannot delete '..'."); return; }
        const std::string path = v.selectedPath();
        const auto token = lifetimeToken();
        ctx_.confirm(U"Are you sure you want to delete?\n" + text::toUtf32(path),
                     [this, token, path](bool ok) {
                         if (!token.lock()) return;
                         if (!ok) { status("Cancelled."); return; }
                         try {
                              fs::removePath(path);
                              reloadBoth();
                              status("Deleted: " + fs::nameOf(path));
                         } catch (const std::exception& e) {
                             status(std::string("Error: ") + e.what());
                         }
                     });
    }

    void refreshBoth() {
        reloadBoth();
        status("Both panels refreshed.");
    }

    void goToFolder() {
        const bool toRight = rightActive_; // which panel requested it when the dialog opened
        const auto token = lifetimeToken();
        ctx_.openFolder(active().currentDir(), [this, token, toRight](const std::string& picked) {
            if (!token.lock()) return;
            (toRight ? right_ : left_).setDirectory(picked);
            status("Panel: " + picked);
        });
    }

    bool isRightActive() const { return rightActive_; }

    // Keep both panels visible while allowing the window to fit smaller TTYs.
    // Each panel gets at least nine columns; one list row plus the status bar.
    int minWidth() const override { return 18; }
    int minHeight() const override { return 3; }

    // --- Widget --------------------------------------------------------------

    void draw(Screen& screen) override {
        const int paneH = std::max(0, bounds_.h - 1);
        const int leftW = bounds_.w / 2;
        left_.setBounds(Rect{bounds_.x, bounds_.y, leftW, paneH});
        right_.setBounds(Rect{bounds_.x + leftW, bounds_.y, bounds_.w - leftW, paneH});
        // Active panel = focused (the ListView header is highlighted in the accent color).
        left_.setFocused(focused_ && !rightActive_);
        right_.setFocused(focused_ && rightActive_);
        left_.draw(screen);
        right_.draw(screen);

        status_.setBounds(Rect{bounds_.x, bounds_.y + paneH, bounds_.w, 1});
        // StatusBar draws the panel label to the end of the bar, so we clip the
        // left text so it doesn't slide underneath and mix with the label.
        const int statusLeftW = bounds_.w - 6; // "Right" (5 chars) + spacing
        status_.setText(text::clip(
                            U"Tab/Arrows: panels",
                            statusLeftW),
                        rightActive_ ? U"Right" : U"Left");
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        // Tab/Left/Right = switch to the other panel (as in mc).
        if ((!ev.ctrl && !ev.alt && ev.key == Key::Tab) ||
            (!ev.ctrl && !ev.alt && (ev.key == Key::Left || ev.key == Key::Right))) {
            rightActive_ = !rightActive_;
            return KeyEventResult::Consumed;
        }
        if (ev.ctrl && ev.key == Key::Char) {
            switch (ev.character) {
                case U'c': case U'C': copySelected();  return KeyEventResult::Consumed;
                case U'x': case U'X': moveSelected();   return KeyEventResult::Consumed;
                case U'r': case U'R': renameSelected(); return KeyEventResult::Consumed;
                case U'g': case U'G': goToFolder();     return KeyEventResult::Consumed;
                default: break;
            }
        }
        if (ev.key == Key::Delete) { deleteSelected(); return KeyEventResult::Consumed; }
        // Backspace also goes through goUp(), not the panel: DirectoryView wouldn't
        // report a status change and the bar would show the old directory.
        if (ev.key == Key::Backspace) { goUp(); return KeyEventResult::Consumed; }
        return active().handleKey(ev); // navigation/Enter goes to the panel
    }

private:
    DirectoryView& active() { return rightActive_ ? right_ : left_; }
    DirectoryView& other() { return rightActive_ ? left_ : right_; }

    void status(const std::string& msg) { status_.setMessage(text::toUtf32(msg)); }

    // "report.txt" when taken -> "report (1).txt" etc. (as in old file managers).
    static std::string uniqueNameIn(const std::string& dir, const std::string& base) {
        std::string dst = fs::join(dir, base);
        if (!fs::exists(dst)) return dst;
        auto dot = base.find_last_of('.');
        const bool hasExt = dot != std::string::npos && dot != base.size() - 1 && dot != 0;
        const std::string stem = hasExt ? base.substr(0, dot) : base;
        const std::string ext = hasExt ? base.substr(dot) : "";
        for (int n = 1;; ++n) {
            dst = fs::join(dir, stem + " (" + std::to_string(n) + ")" + ext);
            if (!fs::exists(dst)) return dst;
        }
    }

    // Reload both panels after a file operation. If a panel was inside
    // a directory that just disappeared (delete) or was renamed (move/
    // rename), its directory no longer exists — without this fix the panel
    // would stay stuck with a red listing error instead of showing content.
    void reloadBoth() {
        left_.reload();
        right_.reload();
        repointIfGone(left_);
        repointIfGone(right_);
    }

    static void repointIfGone(DirectoryView& v) {
        std::string dir = v.currentDir();
        while (!fs::exists(dir)) {
            const std::string up = fs::parent(dir);
            if (up == dir) break; // the root always exists — defensive loop
            dir = up;
        }
        if (dir != v.currentDir()) v.setDirectory(dir);
    }

    // Whether `dir` is `root` or a path inside it. We mind the '/' boundary:
    // "/ab" is NOT inside "/a". Copying/moving a directory into itself
    // or into its own descendant ends in stdfs::copy recursion.
    static bool isWithin(const std::string& dir, const std::string& root) {
        const std::string resolvedDir = resolveForComparison(dir);
        const std::string resolvedRoot = resolveForComparison(root);
        if (resolvedDir == resolvedRoot) return true;
        const std::string prefix = resolvedRoot == "/" ? "/" : resolvedRoot + "/";
        return resolvedDir.compare(0, prefix.size(), prefix) == 0;
    }

    static bool sameDirectory(const std::string& a, const std::string& b) {
        return resolveForComparison(a) == resolveForComparison(b);
    }

    static std::string resolveForComparison(const std::string& path) {
        std::error_code ec;
        const auto resolved = std::filesystem::weakly_canonical(path, ec);
        return ec ? path : resolved.string();
    }

    AppContext& ctx_;
    DirectoryView left_;
    DirectoryView right_;
    bool rightActive_ = false;

    StatusBar status_;
};

} // namespace fenrir::apps
