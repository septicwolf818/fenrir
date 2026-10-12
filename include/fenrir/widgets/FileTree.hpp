#pragma once
#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Drzewo plikow — katalogi rozwijalne [+] / zwijane [-], pliki bez prefiksu,
// wciecie 2 spacje na poziom. Wyswietla katalog root jako korzen (jego wiersz
// pelni tez role naglowka). Enter: plik -> onOpen(path), katalog -> przelacz
// rozwiniecie. Strzalki: G/D nawigacja, P rozwija, L zwija (lub w gore do
// rodzica, gdy wiersz jest juz zwiniety). Katalogi pierwsze — tak jak
// fs::list (sortowanie dziedziczone).
class FileTree : public Widget {
public:
    std::function<void(const std::string& path)> onOpen;

    // Ustawia katalog root i rozwija go; zaznaczenie na korzeniu.
    void setRoot(std::string path) {
        root_ = std::move(path);
        expanded_.clear();
        if (!root_.empty()) expanded_.insert(root_);
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

        // Wybrany wiersz zawsze w widocznym zakresie.
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
                    ++sel_; // do pierwszego potomka
                return KeyEventResult::Consumed;
            }
            case Key::Left: {
                const Node& n = visible_[static_cast<std::size_t>(sel_)];
                if (n.isDir && n.expanded) {
                    toggleExpand(sel_);
                } else if (n.depth > 0) {
                    // do najblizszego przodka (ostatni wiersz nad nami o mniejszym glebokosci)
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

    bool editable() const override { return false; }

private:
    struct Node {
        std::string path;
        int depth = 0;
        bool isDir = false;
        bool expanded = false;
    };

    // Pelna przebudowa widocznej listy (root + rozwinieta galaz).
    // Zaznaczenie trzymamy po SCIEZCE — indeksy zmieniaja sie przy rozwijaniu.
    void rebuild() {
        std::string selPath = (sel_ < static_cast<int>(visible_.size()))
                                  ? visible_[static_cast<std::size_t>(sel_)].path
                                  : std::string();
        visible_.clear();
        if (!root_.empty()) addNode(root_, 0);
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
            return; // brak dostepu / znikniety katalog — pokazujemy sam wiersz
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
        rebuild();
    }

    std::string root_;
    std::vector<Node> visible_;
    std::set<std::string> expanded_;
    int sel_ = 0;
    int scrollRow_ = 0;
};

} // namespace fenrir
