#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace fenrir::apps::handbook {

// The Handbook is the built-in documentation of Fenrir. Every page is built
// from the same small set of blocks, so all pages look and behave the same:
//
//   title    - the name of the page (also its entry in the table of contents)
//   summary  - one line under the title
//   sections - the content: paragraphs, lists, tables of keys, notes, links
//
// A section may carry a heading ("Keys", "Tips", ...) that is drawn above it.

// One row of a key table: the keys and what they do.
struct KeyRow {
    std::string keys;   // e.g. "Ctrl+N"
    std::string action; // e.g. "New tab"
};

// The kinds of content a page can contain.
enum class SectionKind {
    Text,    // a paragraph
    Bullets, // a list of short items ("- item")
    Steps,   // a numbered list ("1. item") - used by the tutorials
    Keys,    // a two-column table of keys and actions
    Note,    // a tip or a warning ("Note: ...")
    SeeAlso, // links to other pages ("1) Page title")
};

// A block of content with an optional heading above it.
struct Section {
    SectionKind kind = SectionKind::Text;
    std::string heading;              // may be empty
    std::string text;                 // Text / Note
    std::vector<std::string> items;   // Bullets / Steps
    std::vector<KeyRow> keys;         // Keys
    std::vector<std::string> links;   // SeeAlso: ids of the linked pages
};

// One help page.
struct Page {
    std::string category; // id of the category it is listed under
    std::string id;       // stable id - also the target of a "see also" link
    std::string title;    // shown in the table of contents
    std::string summary;  // one line under the title
    std::vector<Section> sections;
};

// A group of pages in the table of contents.
struct Category {
    std::string id;
    std::string title;
};

// All pages of the handbook.
class Library {
public:
    Library() = default;

    void addCategory(Category category) { categories_.push_back(std::move(category)); }
    void add(Page page) { pages_.push_back(std::move(page)); }

    const std::vector<Category>& categories() const { return categories_; }
    const std::vector<Page>& pages() const { return pages_; }

    // Index of the page with the given id, or -1 when there is no such page.
    int indexOf(const std::string& id) const {
        for (std::size_t i = 0; i < pages_.size(); ++i)
            if (pages_[i].id == id) return static_cast<int>(i);
        return -1;
    }

    const Page* find(const std::string& id) const {
        const int index = indexOf(id);
        return index < 0 ? nullptr : &pages_[static_cast<std::size_t>(index)];
    }

    // The pages of one category, in the order of the library.
    std::vector<const Page*> pagesOf(const std::string& categoryId) const {
        std::vector<const Page*> out;
        for (const auto& page : pages_)
            if (page.category == categoryId) out.push_back(&page);
        return out;
    }

private:
    std::vector<Category> categories_;
    std::vector<Page> pages_;
};

// Builds a page with the common structure. The heading set by section() is
// used by every block that follows it, until another heading is set.
class PageBuilder {
public:
    PageBuilder(std::string category, std::string id, std::string title)
        : page_{std::move(category), std::move(id), std::move(title), {}, {}} {}

    PageBuilder& summary(std::string text) {
        page_.summary = std::move(text);
        return *this;
    }

    // Sets the heading of the blocks that follow (sticky until changed).
    PageBuilder& section(std::string heading) {
        heading_ = std::move(heading);
        return *this;
    }

    PageBuilder& text(std::string body) {
        Section s;
        s.kind = SectionKind::Text;
        s.heading = heading_;
        s.text = std::move(body);
        page_.sections.push_back(std::move(s));
        return *this;
    }

    PageBuilder& bullets(std::vector<std::string> items) {
        Section s;
        s.kind = SectionKind::Bullets;
        s.heading = heading_;
        s.items = std::move(items);
        page_.sections.push_back(std::move(s));
        return *this;
    }

    PageBuilder& steps(std::vector<std::string> items) {
        Section s;
        s.kind = SectionKind::Steps;
        s.heading = heading_;
        s.items = std::move(items);
        page_.sections.push_back(std::move(s));
        return *this;
    }

    PageBuilder& keys(std::vector<KeyRow> keys) {
        Section s;
        s.kind = SectionKind::Keys;
        s.heading = heading_;
        s.keys = std::move(keys);
        page_.sections.push_back(std::move(s));
        return *this;
    }

    PageBuilder& note(std::string body) {
        Section s;
        s.kind = SectionKind::Note;
        s.heading = heading_;
        s.text = std::move(body);
        page_.sections.push_back(std::move(s));
        return *this;
    }

    // Links to other pages of the handbook (by id).
    PageBuilder& seeAlso(std::vector<std::string> pageIds) {
        Section s;
        s.kind = SectionKind::SeeAlso;
        s.heading = "See also";
        s.links = std::move(pageIds);
        page_.sections.push_back(std::move(s));
        return *this;
    }

    Page build() const { return page_; }

private:
    Page page_;
    std::string heading_;
};

// Lower-cased copy of a string (ASCII) - used by the search.
inline std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

// Whether the page mentions the needle (title, summary, or any of its
// content). The comparison ignores the case.
inline bool pageMatches(const Page& page, const std::string& needle) {
    if (needle.empty()) return true;
    const std::string n = toLower(needle);
    auto contains = [&](const std::string& haystack) {
        return toLower(haystack).find(n) != std::string::npos;
    };
    if (contains(page.title) || contains(page.summary)) return true;
    for (const auto& section : page.sections) {
        if (contains(section.heading) || contains(section.text)) return true;
        for (const auto& item : section.items)
            if (contains(item)) return true;
        for (const auto& key : section.keys)
            if (contains(key.keys) || contains(key.action)) return true;
    }
    return false;
}

} // namespace fenrir::apps::handbook
