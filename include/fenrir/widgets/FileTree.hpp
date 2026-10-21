#pragma once
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// File tree — expandable directories [+] / collapsed [-], files without a prefix,
// 2 spaces of indent per level. Shows the root directory as the root (its row
// also serves as the header). Enter: file -> onOpen(path), directory -> toggle
// expansion. Arrows: Up/Down navigate, Right expands, Left collapses (or moves
// up to the parent when the row is already collapsed). Directories first — as in
// fs::list (sorting inherited).
class FileTree : public Widget {
public:
    std::function<void(const std::string& path)> onOpen;

    // Sets the root directory and expands it; selection on the root.
    void setRoot(std::string path) {
        root_ = std::move(path);
        expanded_.clear();
        if (!root_.empty()) expanded_.insert(root_);
        // Reset timestamps for new root
        dirTimestamps_.clear();
        lastPoll_ = std::chrono::steady_clock::time_point{};
        rebuild();
        sel_ = 0;
        scrollRow_ = 0;
    }

    const std::string& root() const { return root_; }
    int selected() const { return sel_; }

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.editorText, t.editorBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0 || visible_.empty()) return;

        // Poll for changes in opened directories before drawing
        pollChanges();

        // Keep the selected row within the visible range.
        if (sel_ < scrollRow_) scrollRow_ = sel_;
        if (sel_ >= scrollRow_ + bounds_.h) scrollRow_ = sel_ - bounds_.h + 1;
        scrollRow_ = std::max(0, scrollRow_);

        for (int row = 0; row < bounds_.h; ++row) {
            const int idx = scrollRow_ + row;
            if (idx >= static_cast<int>(visible_.size())) break;
            const Node& n = visible_[static_cast<std::size_t>(idx)];
            const bool sel = (idx == sel_);

            std::u32string line(static_cast<std::size_t>(n.depth * 2), U' ');
            if (n.isDir) line += n.expanded ? U"[-] " : U"[+] ";
            else line += U"    ";
            line += text::toUtf32(fs::nameOf(n.path));
            if (static_cast<int>(line.size()) > bounds_.w)
                line.resize(static_cast<std::size_t>(bounds_.w));

            const Color bg = sel ? t.accentBg : t.editorBg;
            const Color fg = sel ? t.accentText : (n.isDir ? t.windowText : t.editorText);
            screen.fillRect(Rect{bounds_.x, bounds_.y + row, bounds_.w, 1},
                            Cell{U' ', fg, bg, 0});
            screen.putText(bounds_.x, bounds_.y + row, line, fg, bg,
                           sel ? StyleBold : StyleNone);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (visible_.empty()) return KeyEventResult::Consumed;
        const int last = static_cast<int>(visible_.size()) - 1;
        switch (ev.key) {
            case Key::Up:   sel_ = std::max(0, sel_ - 1); return KeyEventResult::Consumed;
            case Key::Down: sel_ = std::min(last, sel_ + 1); return KeyEventResult::Consumed;
            case Key::PageUp:   sel_ = std::max(0, sel_ - std::max(1, bounds_.h)); return KeyEventResult::Consumed;
            case Key::PageDown: sel_ = std::min(last, sel_ + std::max(1, bounds_.h)); return KeyEventResult::Consumed;
            case Key::Home: sel_ = 0; return KeyEventResult::Consumed;
            case Key::End:  sel_ = last; return KeyEventResult::Consumed;
            case Key::Right: {
                const Node& n = visible_[static_cast<std::size_t>(sel_)];
                if (n.isDir && !n.expanded) toggleExpand(sel_);
                else if (n.isDir && sel_ < last &&
                         visible_[static_cast<std::size_t>(sel_ + 1)].depth > n.depth)
                    ++sel_; // to the first child
                return KeyEventResult::Consumed;
            }
            case Key::Left: {
                const Node& n = visible_[static_cast<std::size_t>(sel_)];
                if (n.isDir && n.expanded) {
                    toggleExpand(sel_);
                } else if (n.depth > 0) {
                    // to the nearest ancestor (the last row above us with smaller depth)
                    for (int i = sel_ - 1; i >= 0; --i) {
                        if (visible_[static_cast<std::size_t>(i)].depth < n.depth) {
                            sel_ = i;
                            break;
                        }
                    }
                }
                return KeyEventResult::Consumed;
            }
            case Key::Enter: {
                const Node& n = visible_[static_cast<std::size_t>(sel_)];
                if (n.isDir) toggleExpand(sel_);
                else if (onOpen) onOpen(n.path);
                return KeyEventResult::Consumed;
            }
            default: return KeyEventResult::Ignored;
        }
    }

    bool clickAt(int x, int y) override {
        if (!bounds_.contains(x, y)) return false;
        const int index = scrollRow_ + (y - bounds_.y);
        if (index < 0 || index >= static_cast<int>(visible_.size())) return false;
        sel_ = index;
        return true;
    }

    bool editable() const override { return false; }

private:
    struct Node {
        std::string path;
        int depth = 0;
        bool isDir = false;
        bool expanded = false;
    };

    // Full rebuild of the visible list (root + expanded branch).
    // The selection is tracked by PATH — indices change while expanding.
    void rebuild() {
        std::string selPath = (sel_ < static_cast<int>(visible_.size()))
                                  ? visible_[static_cast<std::size_t>(sel_)].path
                                  : std::string();
        visible_.clear();
        if (!root_.empty()) addNode(root_, 0);
        // Update timestamps for expanded directories after rebuild
        for (const auto& p : expanded_) {
            updateTimestamp(p);
        }
        sel_ = 0;
        if (!selPath.empty()) {
            for (int i = 0; i < static_cast<int>(visible_.size()); ++i) {
                if (visible_[static_cast<std::size_t>(i)].path == selPath) {
                    sel_ = i;
                    break;
                }
            }
        }
        if (sel_ >= static_cast<int>(visible_.size()))
            sel_ = std::max(0, static_cast<int>(visible_.size()) - 1);
    }

    void addNode(const std::string& path, int depth) {
        const bool exp = expanded_.count(path) > 0;
        visible_.push_back(Node{path, depth, true, exp});
        if (!exp) return;
        std::vector<fs::Entry> entries;
        try {
            entries = fs::list(path);
        } catch (...) {
            return; // no access / vanished directory — show just the row
        }
        for (const auto& e : entries) {
            const std::string full = fs::join(path, e.name);
            if (e.isDir) addNode(full, depth + 1);
            else visible_.push_back(Node{full, depth + 1, false, false});
        }
    }

    void toggleExpand(int idx) {
        Node& n = visible_[static_cast<std::size_t>(idx)];
        if (n.expanded) expanded_.erase(n.path);
        else expanded_.insert(n.path);
        // Rescan on collapse/expand so changes are picked up immediately
        // Update timestamp for the toggled directory to avoid immediate re-poll
        updateTimestamp(n.path);
        rebuild();
    }

    // Watchdog: poll only expanded directories for changes every 500ms.
    void pollChanges() {
        using namespace std::chrono;
        const auto now = steady_clock::now();
        if (lastPoll_ == time_point<steady_clock>{}) {
            lastPoll_ = now;
            // Initial population of timestamps
            for (const auto& p : expanded_) {
                updateTimestamp(p);
            }
            return;
        }
        if (now - lastPoll_ < milliseconds(500)) return;
        lastPoll_ = now;

        bool changed = false;
        // Only scan directories that are currently expanded (and visible in tree)
        std::vector<std::string> toCheck;
        toCheck.reserve(expanded_.size());
        for (const auto& p : expanded_) {
            toCheck.push_back(p);
        }
        for (const auto& p : toCheck) {
            std::filesystem::path fsPath(p);
            std::error_code ec;
            auto status = std::filesystem::status(fsPath, ec);
            if (ec) {
                // Directory vanished or inaccessible; remove from expanded and mark changed
                if (expanded_.count(p)) {
                    expanded_.erase(p);
                    changed = true;
                }
                dirTimestamps_.erase(p);
                continue;
            }
            // Check if directory is still expanded (might have been collapsed during iteration)
            if (!expanded_.count(p)) continue;
            auto mtime = std::filesystem::last_write_time(fsPath, ec);
            if (ec) continue;
            auto it = dirTimestamps_.find(p);
            if (it == dirTimestamps_.end()) {
                dirTimestamps_[p] = mtime;
            } else if (it->second != mtime) {
                // Directory modification time changed — rebuild
                changed = true;
                it->second = mtime;
            }
        }
        if (changed) rebuild();
    }

    void updateTimestamp(const std::string& path) {
        std::filesystem::path fsPath(path);
        std::error_code ec;
        auto status = std::filesystem::status(fsPath, ec);
        if (ec) return;
        auto mtime = std::filesystem::last_write_time(fsPath, ec);
        if (ec) return;
        dirTimestamps_[path] = mtime;
    }

    std::string root_;
    std::vector<Node> visible_;
    std::set<std::string> expanded_;
    int sel_ = 0;
    int scrollRow_ = 0;
    std::unordered_map<std::string, std::filesystem::file_time_type> dirTimestamps_;
    std::chrono::steady_clock::time_point lastPoll_;
};

} // namespace fenrir
