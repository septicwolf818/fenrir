#pragma once
#include <functional>
#include <string>
#include <utility>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/FileWatchdog.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/DirectoryListing.hpp"
#include "fenrir/widgets/ListView.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Directory view = model (DirectoryListing) + list (ListView) spliced into a single
// widget: path header, scrollable list, entering directories (Enter), going
// up (Backspace), listing errors drawn in place. The ONLY place that handles
// "directory browsing" — FileDialog and the file manager compose TWO such views
// instead of writing their own list drawing (a duplication that had crept into
// the project in three places).
//
// The listed directory is watched, so files created, renamed or removed
// elsewhere show up without pressing a refresh key.
class DirectoryView : public Widget {
public:
    explicit DirectoryView(std::string startDir) : listing_(std::move(startDir)) {
        list_.onActivate = [this](int idx) { activateIndex(idx); };
        watchdogPath_ = listing_.currentDir();
        watchdog_.watch(watchdogPath_);
        refresh(/*keepSelection=*/false);
    }

    std::function<void(const std::string& path)> onFileActivate; // Enter on a file
    std::function<void(const std::string& dir)> onDirChange;     // after the directory changes
    std::function<void(const std::string& msg)> onStatus;        // status message like "Entered: ..."

    const std::string& currentDir() const { return listing_.currentDir(); }
    bool hasError() const { return listing_.hasError(); }
    const std::string& error() const { return listing_.error(); }
    bool empty() const { return listing_.empty(); }

    int selectedIndex() const { return list_.selectedIndex(); }
    void setSelectedIndex(int idx) { list_.setSelectedIndex(idx); }
    bool selectedIsFile() const { return listing_.isFileAt(list_.selectedIndex()); }
    bool selectedIsParent() const { return listing_.isParentAt(list_.selectedIndex()); }
    std::string selectedPath() const { return listing_.pathAt(list_.selectedIndex()); }

    // Switch to the given directory (Ctrl+G / "Go to folder...").
    void setDirectory(std::string dir) {
        listing_ = DirectoryListing(std::move(dir));
        watchdog_.unwatch(watchdogPath_);
        watchdogPath_ = listing_.currentDir();
        watchdog_.watch(watchdogPath_);
        refresh(/*keepSelection=*/false);
        if (onDirChange) onDirChange(listing_.currentDir());
    }

    void goUp() {
        const std::string before = listing_.currentDir();
        listing_.goUp();
        watchdog_.unwatch(watchdogPath_);
        watchdogPath_ = listing_.currentDir();
        watchdog_.watch(watchdogPath_);
        refresh(/*keepSelection=*/false);
        if (onDirChange && listing_.currentDir() != before) onDirChange(listing_.currentDir());
    }

    // Re-read the same directory from DISK (after copy/move/delete/rename) —
    // the selection stays on the same index (clamped) so it doesn't jump to the
    // top. Previously listing_.reload() was missing: we only refreshed the view
    // from the in-memory model, so after file operations the panels kept stale
    // entries — copying "did nothing".
    void reload() {
        listing_.reload();
        refresh(/*keepSelection=*/true);
    }

    // Is the selected directory ".."? Deleting/renaming ".." would touch the
    // parent directory — guard for the caller (see the file manager).
    bool selectionIsSafeTarget() const { return !listing_.isParentAt(list_.selectedIndex()); }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        if (bounds_.w <= 0 || bounds_.h <= 0) return;

        // The listed directory changed on disk (a file was created, renamed
        // or removed elsewhere) — re-read it. The selection stays put. No
        // status message: the list visibly updates, and a message here would
        // overwrite the one the file manager shows for its own actions.
        for (const auto& event : watchdog_.poll()) {
            if (event.path != watchdogPath_) continue;
            listing_.reload();
            refresh(/*keepSelection=*/true);
        }

        if (listing_.hasError()) {
            // Listing error: a clear red message instead of an empty list.
            screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
            screen.fillRect(Rect{bounds_.x, bounds_.y, bounds_.w, 1},
                            Cell{U' ', t.headerText, t.headerBg, StyleBold});
            screen.putText(bounds_.x, bounds_.y, text::clip(text::toUtf32(listing_.currentDir()), bounds_.w),
                           t.headerText, t.headerBg, StyleBold);
            screen.putText(bounds_.x, bounds_.y + 1, text::clip(text::toUtf32(listing_.error()), bounds_.w),
                           t.errorText, t.surfaceBg, StyleBold);
            return;
        }

        list_.setBounds(bounds_);
        list_.setFocused(focused_);
        list_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.key == Key::Backspace) {
            const std::string before = listing_.currentDir();
            goUp();
            if (listing_.currentDir() != before) return KeyEventResult::Consumed;
        }
        return list_.handleKey(ev);
    }

    bool activate() override { return list_.activate(); }

    bool selectable() const override { return enabled_; }

private:
    void refresh(bool keepSelection) {
        const int prev = list_.selectedIndex();
        list_.setHeader(text::toUtf32(listing_.currentDir()));
        list_.setItems(listing_.items());
        if (keepSelection) list_.setSelectedIndex(prev);
        else list_.setSelectedIndex(0);
    }

    void activateIndex(int idx) {
        if (listing_.enterAt(idx)) {
            refresh(/*keepSelection=*/false);
            if (onDirChange) onDirChange(listing_.currentDir());
            if (onStatus) onStatus("Entered: " + listing_.currentDir());
            return;
        }
        if (onFileActivate) onFileActivate(listing_.pathAt(idx));
    }

    DirectoryListing listing_;
    ListView list_;
    FileWatchdog watchdog_;
    std::string watchdogPath_; // the directory currently watched
};

} // namespace fenrir
