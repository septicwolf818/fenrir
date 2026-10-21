#pragma once
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "fenrir/app/App.hpp"
#include "fenrir/app/AppContext.hpp"
#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"
#include "fenrir/widgets/StatusBar.hpp"
#include "fenrir/widgets/Widget.hpp"

#include "HandbookContent.hpp"
#include "HelpLayout.hpp"
#include "HelpPage.hpp"

namespace fenrir::apps {

// The table of contents of the Handbook: the categories with their pages.
// A widget of its own because the entries have two levels (a category header
// and the pages below it) - the flat SelectionList cannot show that.
class HandbookToc : public Widget {
public:
    struct Entry {
        bool category = false; // a category header (not selectable)
        std::string label;
        int pageIndex = -1;    // the page in the library (-1 = a header)
    };

    void setEntries(std::vector<Entry> entries) {
        entries_ = std::move(entries);
        selected_ = firstPageEntry();
        scroll_ = 0;
    }

    const std::vector<Entry>& entries() const { return entries_; }
    int count() const { return static_cast<int>(entries_.size()); }
    int selected() const { return selected_; }
    bool empty() const { return entries_.empty(); }

    // The page the selection is on (-1 when there is none).
    int selectedPage() const {
        if (selected_ < 0 || selected_ >= count()) return -1;
        return entries_[static_cast<std::size_t>(selected_)].pageIndex;
    }

    // Whether the page is in the list (a filtered list may not hold it).
    bool hasPage(int pageIndex) const {
        for (const auto& entry : entries_)
            if (entry.pageIndex == pageIndex) return true;
        return false;
    }

    // Selects the entry of the given page.
    void selectPage(int pageIndex) {
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].pageIndex == pageIndex) {
                selected_ = static_cast<int>(i);
                ensureVisible();
                return;
            }
        }
    }

    // Moves the selection to the next (delta > 0) or previous page entry.
    // The category headers are skipped.
    void moveSelection(int delta) {
        if (entries_.empty()) return;
        if (selected_ < 0) selected_ = firstPageEntry();
        const int direction = delta < 0 ? -1 : 1;
        for (int step = 0; step < std::abs(delta); ++step) {
            int next = selected_;
            bool found = false;
            for (int scan = 0; scan < count(); ++scan) {
                next += direction;
                if (next < 0 || next >= count()) break;
                if (entries_[static_cast<std::size_t>(next)].pageIndex >= 0) {
                    selected_ = next;
                    ensureVisible();
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
    }

    // Called with the page index when the selection is activated.
    std::function<void(int pageIndex)> onSelect;

    void draw(Screen& screen) override {
        const auto& t = theme::active;
        screen.fillRect(bounds_, Cell{U' ', t.surfaceText, t.surfaceBg, 0});
        if (bounds_.w <= 0 || bounds_.h <= 0) return;
        ensureVisible();

        for (int row = 0; row < bounds_.h; ++row) {
            const int index = scroll_ + row;
            if (index >= count()) break;
            const Entry& entry = entries_[static_cast<std::size_t>(index)];
            const bool sel = index == selected_;
            Color fg = sel ? t.accentText : t.surfaceText;
            Color bg = sel ? t.accentBg : t.surfaceBg;
            uint8_t style = StyleNone;
            if (entry.category) {
                // The category headers stand out, but they are never selected.
                fg = t.buttonTextHot;
                style = StyleBold;
            } else if (sel && !focused_) {
                // The list is not the active pane: the selection is dimmed.
                fg = t.dimText;
            }
            screen.fillRect(Rect{bounds_.x, bounds_.y + row, bounds_.w, 1},
                            Cell{U' ', fg, bg, style});
            const std::u32string label = text::toUtf32(
                entry.category ? entry.label : "  " + entry.label);
            screen.putText(bounds_.x, bounds_.y + row, text::truncate(label, bounds_.w), fg, bg, style);
        }
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (entries_.empty()) return KeyEventResult::Ignored;
        const int before = selected_;
        switch (ev.key) {
            case Key::Up:       moveSelection(-1); break;
            case Key::Down:     moveSelection(+1); break;
            case Key::PageUp:   moveSelection(-std::max(1, bounds_.h)); break;
            case Key::PageDown: moveSelection(+std::max(1, bounds_.h)); break;
            case Key::Home:     selectEdge(true); break;
            case Key::End:      selectEdge(false); break;
            case Key::Enter:    return activate() ? KeyEventResult::Activated
                                                  : KeyEventResult::Ignored;
            default: return KeyEventResult::Ignored;
        }
        return selected_ != before ? KeyEventResult::Consumed : KeyEventResult::Ignored;
    }

    bool activate() override {
        const int page = selectedPage();
        if (page < 0) return false;
        if (onSelect) onSelect(page);
        return true;
    }

private:
    int firstPageEntry() const {
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].pageIndex >= 0) return static_cast<int>(i);
        return entries_.empty() ? -1 : 0;
    }

    void selectEdge(bool first) {
        if (first) {
            selected_ = firstPageEntry();
        } else {
            for (int i = count() - 1; i >= 0; --i) {
                if (entries_[static_cast<std::size_t>(i)].pageIndex >= 0) {
                    selected_ = i;
                    break;
                }
            }
        }
        ensureVisible();
    }

    void ensureVisible() {
        const int rows = std::max(0, bounds_.h);
        if (rows == 0 || entries_.empty()) return;
        if (selected_ < 0) selected_ = firstPageEntry();
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + rows) scroll_ = selected_ - rows + 1;
        scroll_ = std::clamp(scroll_, 0, std::max(0, count() - rows));
    }

    std::vector<Entry> entries_;
    int selected_ = -1;
    int scroll_ = 0;
};

// The Handbook - the built-in documentation of Fenrir: the tutorials and the
// reference pages for the desktop, the modes, and every application.
//
// The window has two panes: the table of contents on the left and the page on
// the right. Tab (or Left/Right) switches between them; the contents follow
// the selection, and the numbered links at the end of a page open the related
// page (press the digit).
class HandbookView : public Widget {
public:
    explicit HandbookView(AppContext& ctx) : ctx_(ctx), library_(handbook::buildLibrary()) {
        toc_.onSelect = [this](int) { focus_ = Focus::Content; updateStatus(); };
        rebuildToc();
        showPage(toc_.selectedPage());
    }

    // --- Actions offered in the app menu -------------------------------------

    void search() {
        const auto token = lifetimeToken();
        ctx_.prompt(U"Search", U"Search the handbook:", U"Text: ", text::toUtf32(filter_),
                    [this, token](std::optional<std::u32string> value) {
                        if (!token.lock()) return;
                        if (!value) return; // Cancel keeps the current filter.
                        filter_ = text::toUtf8(*value);
                        rebuildToc();
                        if (!filter_.empty()) {
                            if (toc_.selectedPage() >= 0) {
                                showPage(toc_.selectedPage());
                            } else {
                                currentPage_ = -1;
                                lines_.clear();
                                layoutDirty_ = false;
                                scroll_ = 0;
                            }
                        } else if (currentPage_ >= 0) {
                            showPage(currentPage_);
                        } else if (toc_.selectedPage() >= 0) {
                            showPage(toc_.selectedPage());
                        }
                        updateStatus();
                    });
    }

    void clearSearch() {
        filter_.clear();
        rebuildToc();
        if (currentPage_ >= 0) showPage(currentPage_);
        else if (toc_.selectedPage() >= 0) showPage(toc_.selectedPage());
        updateStatus();
    }

    void nextPage() { stepPage(+1); }
    void prevPage() { stepPage(-1); }

    // --- State (the tests and the app menu use them) -------------------------

    std::string currentPageId() const {
        if (currentPage_ < 0) return {};
        return library_.pages()[static_cast<std::size_t>(currentPage_)].id;
    }

    std::string currentPageTitle() const {
        if (currentPage_ < 0) return {};
        return library_.pages()[static_cast<std::size_t>(currentPage_)].title;
    }

    // The text of the search filter (empty when there is none).
    const std::string& filter() const { return filter_; }
    // The page selected in the table of contents.
    int tocPage() const { return toc_.selectedPage(); }

    bool editable() const override { return false; }
    int minWidth() const override { return 40; }
    int minHeight() const override { return 8; }

    // --- Widget ---------------------------------------------------------------

    void draw(Screen& screen) override {
        const int paneHeight = std::max(0, bounds_.h - 1); // the status bar takes the last row
        const int totalWidth = std::max(0, bounds_.w);
        const int tocWidth = std::min(std::clamp(totalWidth / 3, 16, 28), totalWidth);

        toc_.setBounds(Rect{bounds_.x, bounds_.y, tocWidth, paneHeight});
        toc_.setFocused(focused_ && focus_ == Focus::Toc);
        toc_.draw(screen);

        // A vertical line separates the contents from the page.
        const int separatorX = bounds_.x + tocWidth;
        const auto& t = theme::active;
        for (int row = 0; row < paneHeight; ++row)
            screen.putText(separatorX, bounds_.y + row, U"|", t.borderInactive, t.editorBg);

        const Rect content{separatorX + 1, bounds_.y,
                           std::max(0, bounds_.w - tocWidth - 1), paneHeight};
        drawContent(screen, content);

        status_.setBounds(Rect{bounds_.x, bounds_.y + paneHeight, bounds_.w, 1});
        status_.draw(screen);
    }

    KeyEventResult handleKey(const KeyEvent& ev) override {
        if (ev.ctrl && ev.key == Key::Char && ev.character == U'f') {
            search();
            return KeyEventResult::Consumed;
        }
        // Tab and the horizontal arrows switch between the two panes.
        if (!ev.ctrl && !ev.alt &&
            (ev.key == Key::Tab || ev.key == Key::Left || ev.key == Key::Right)) {
            focus_ = (focus_ == Focus::Toc) ? Focus::Content : Focus::Toc;
            updateStatus();
            return KeyEventResult::Consumed;
        }

        if (focus_ == Focus::Toc) {
            const int before = toc_.selected();
            const KeyEventResult result = toc_.handleKey(ev);
            if (toc_.selected() != before) showPage(toc_.selectedPage());
            return result;
        }

        // The page: scrolling, and the numbered links at the end of it.
        ensureLayout(); // the scroll limits depend on the laid-out page
        switch (ev.key) {
            case Key::Up:       scrollBy(-1); break;
            case Key::Down:     scrollBy(+1); break;
            case Key::PageUp:   scrollBy(-std::max(1, contentHeight())); break;
            case Key::PageDown: scrollBy(+std::max(1, contentHeight())); break;
            case Key::Home:     scrollTo(0); break;
            case Key::End:      scrollTo(maxScroll()); break;
            case Key::Char: {
                if (ev.ctrl || ev.alt) return KeyEventResult::Ignored;
                if (ev.character < U'1' || ev.character > U'9') return KeyEventResult::Ignored;
                followLink(static_cast<int>(ev.character - U'0'));
                return KeyEventResult::Consumed;
            }
            default: return KeyEventResult::Ignored;
        }
        return KeyEventResult::Consumed;
    }

private:
    enum class Focus { Toc, Content };

    // --- The contents ---------------------------------------------------------

    void rebuildToc() {
        std::vector<HandbookToc::Entry> entries;
        for (const auto& category : library_.categories()) {
            std::vector<HandbookToc::Entry> group;
            for (const auto* page : library_.pagesOf(category.id)) {
                if (!filter_.empty() && !handbook::pageMatches(*page, filter_)) continue;
                group.push_back(HandbookToc::Entry{false, page->title, library_.indexOf(page->id)});
            }
            if (group.empty()) continue;
            entries.push_back(HandbookToc::Entry{true, category.title, -1});
            entries.insert(entries.end(), group.begin(), group.end());
        }
        toc_.setEntries(std::move(entries));
        // Keep the current page selected when the list still holds it.
        if (currentPage_ >= 0 && toc_.hasPage(currentPage_)) toc_.selectPage(currentPage_);
    }

    // --- The page -------------------------------------------------------------

    void showPage(int pageIndex) {
        if (pageIndex < 0 || pageIndex >= static_cast<int>(library_.pages().size())) return;
        currentPage_ = pageIndex;
        toc_.selectPage(pageIndex);
        layoutDirty_ = true;
        scroll_ = 0;
        updateStatus();
    }

    // The links of the current page, in the order they are printed.
    std::vector<std::string> linksOfCurrentPage() const {
        std::vector<std::string> links;
        if (currentPage_ < 0) return links;
        for (const auto& section : library_.pages()[static_cast<std::size_t>(currentPage_)].sections)
            if (section.kind == handbook::SectionKind::SeeAlso)
                links.insert(links.end(), section.links.begin(), section.links.end());
        return links;
    }

    void followLink(int number) {
        const auto links = linksOfCurrentPage();
        if (number < 1 || number > static_cast<int>(links.size())) {
            status_.setMessage(U"No such link on this page.");
            return;
        }
        const int index = library_.indexOf(links[static_cast<std::size_t>(number - 1)]);
        if (index < 0) {
            status_.setMessage(U"Broken link.");
            return;
        }
        // The target may be outside the filtered list - then the filter is
        // dropped, so the contents show where the link leads.
        if (!toc_.hasPage(index)) {
            filter_.clear();
            rebuildToc();
        }
        showPage(index);
    }

    void stepPage(int delta) {
        const auto& entries = toc_.entries();
        int position = -1;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].pageIndex == currentPage_) {
                position = static_cast<int>(i);
                break;
            }
        }
        if (position < 0) {
            if (toc_.selectedPage() >= 0) showPage(toc_.selectedPage());
            return;
        }
        int index = position;
        for (int step = 0; step < static_cast<int>(entries.size()); ++step) {
            index += delta;
            if (index < 0 || index >= static_cast<int>(entries.size())) return;
            if (entries[static_cast<std::size_t>(index)].pageIndex >= 0) {
                showPage(entries[static_cast<std::size_t>(index)].pageIndex);
                return;
            }
        }
    }

    // --- Drawing the page -----------------------------------------------------

    int contentWidth() const {
        const int totalWidth = std::max(0, bounds_.w);
        const int tocWidth = std::min(std::clamp(totalWidth / 3, 16, 28), totalWidth);
        return std::max(0, totalWidth - tocWidth - 1);
    }

    int contentHeight() const { return std::max(0, bounds_.h - 1); }

    void ensureLayout() {
        const int width = contentWidth();
        if (!layoutDirty_ && width == laidOutWidth_) return;
        if (currentPage_ < 0) {
            lines_.clear();
        } else {
            lines_ = handbook::layoutPage(library_.pages()[static_cast<std::size_t>(currentPage_)],
                                          library_, width);
        }
        laidOutWidth_ = width;
        layoutDirty_ = false;
        scroll_ = std::clamp(scroll_, 0, maxScroll());
    }

    int maxScroll() const {
        return std::max(0, static_cast<int>(lines_.size()) - contentHeight());
    }

    void scrollBy(int delta) {
        scroll_ = std::clamp(scroll_ + delta, 0, maxScroll());
    }

    void scrollTo(int offset) { scroll_ = std::clamp(offset, 0, maxScroll()); }

    void drawContent(Screen& screen, const Rect& area) {
        const auto& t = theme::active;
        screen.fillRect(area, Cell{U' ', t.editorText, t.editorBg, 0});
        if (area.w <= 0 || area.h <= 0) return;
        ensureLayout();

        if (currentPage_ < 0) {
            screen.putText(area.x, area.y, text::clip(U"No pages match this search.", area.w),
                           t.docHeading, t.editorBg, StyleBold);
            return;
        }

        for (int row = 0; row < area.h; ++row) {
            const int index = scroll_ + row;
            if (index >= static_cast<int>(lines_.size())) break;
            const auto& line = lines_[static_cast<std::size_t>(index)];
            const Color bg = line.band ? t.titleBg : t.editorBg;
            if (line.band)
                screen.fillRect(Rect{area.x, area.y + row, area.w, 1}, Cell{U' ', t.titleText, bg, 0});

            int x = area.x;
            for (const auto& segment : line.segments) {
                const int room = area.x + area.w - x;
                if (room <= 0) break;
                const std::u32string visible = text::clip(segment.text, room);
                screen.putText(x, area.y + row, visible, segment.fg, bg,
                               segment.bold ? StyleBold : StyleNone);
                x += static_cast<int>(visible.size());
            }
        }
    }

    void updateStatus() {
        std::u32string left = U"Handbook";
        if (currentPage_ >= 0)
            left = text::toUtf32(library_.pages()[static_cast<std::size_t>(currentPage_)].title);
        std::u32string right = focus_ == Focus::Toc ? U"Contents" : U"Page";
        if (!filter_.empty()) right = U"Filter: " + text::toUtf32(filter_);
        status_.setText(left, right);
    }

    AppContext& ctx_;
    handbook::Library library_;
    HandbookToc toc_;
    StatusBar status_;
    std::vector<handbook::Line> lines_;
    int currentPage_ = -1;
    int scroll_ = 0;
    int laidOutWidth_ = -1;
    bool layoutDirty_ = true;
    Focus focus_ = Focus::Toc;
    std::string filter_;
};

// The Handbook application.
class HandbookApp : public App {
public:
    std::string name() const override { return "Handbook"; }
    // Stable id for file associations and other framework lookups.
    std::string id() const override { return "handbook"; }

    std::unique_ptr<Widget> createView(AppContext& ctx) override {
        auto view = std::make_unique<HandbookView>(ctx);
        HandbookView* raw = view.get();

        view->setMenuTabs({{U"Go", std::vector<MenuItem>{
            {U"Next page (Ctrl+N)", [raw] { raw->nextPage(); }},
            {U"Previous page (Ctrl+P)", [raw] { raw->prevPage(); }},
            {U"Search... (Ctrl+F)", [raw] { raw->search(); }},
            {U"Clear search", [raw] { raw->clearSearch(); }},
        }}});
        addExitMenu(*view, ctx);
        addAboutMenu(*view, ctx, U"Handbook",
                     U"The built-in documentation of Fenrir: the desktop, the modes, and every "
                     U"application.");

        view->pushShortcut({true, Key::Char, U'f'}, [raw] { raw->search(); });
        view->pushShortcut({true, Key::Char, U'n'}, [raw] { raw->nextPage(); });
        view->pushShortcut({true, Key::Char, U'p'}, [raw] { raw->prevPage(); });

        return view;
    }

    Rect preferredBounds(const Rect& desktopArea) const override {
        const int w = std::min(78, std::max(40, desktopArea.w - 4));
        const int h = std::min(22, std::max(10, desktopArea.h - 3));
        return Rect{desktopArea.x + std::max(0, (desktopArea.w - w) / 2),
                    desktopArea.y + std::max(0, (desktopArea.h - h) / 2), w, h};
    }
};

} // namespace fenrir::apps
