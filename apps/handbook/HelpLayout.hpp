#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/Text.hpp"
#include "fenrir/core/Theme.hpp"

#include "HelpPage.hpp"

namespace fenrir::apps::handbook {

// A piece of a laid-out line: the text with its own color and style.
struct Segment {
    std::u32string text;
    Color fg = colors::White;
    bool bold = false;
};

// One visual line of a page.
struct Line {
    std::vector<Segment> segments;
    // The page title is drawn as a band: the whole row gets the band color.
    bool band = false;

    // The line as plain text (used by the tests).
    std::u32string plain() const {
        std::u32string out;
        for (const auto& segment : segments) out += segment.text;
        return out;
    }
};

// Appends a segment to a line (empty text is ignored).
inline void appendSegment(Line& line, std::u32string text, Color fg, bool bold) {
    if (text.empty()) return;
    line.segments.push_back(Segment{std::move(text), fg, bold});
}

// A line with a single piece of text.
inline Line textLine(std::u32string text, Color fg, bool bold) {
    Line line;
    appendSegment(line, std::move(text), fg, bold);
    return line;
}

// An empty line (a vertical gap).
inline Line blankLine() { return Line{}; }

// Wraps `body` to `width` cells and appends the resulting lines to `out`.
// The first line is prefixed with `marker`, the continuation lines are
// indented by `indent` cells (a hanging indent).
inline void appendMarked(std::vector<Line>& out, const std::u32string& marker,
                         const std::u32string& body, int width, Color markerFg, bool markerBold,
                         Color bodyFg, bool bodyBold, int indent) {
    // The body is wrapped so that the marker (first line) and the indent
    // (the continuation lines) still fit into the width.
    const int markerWidth = static_cast<int>(marker.size());
    const int bodyWidth = std::max(1, width - std::max(markerWidth, indent));
    const std::vector<std::u32string> wrapped = text::wrap(body, bodyWidth);
    if (wrapped.empty()) {
        out.push_back(textLine(marker, markerFg, markerBold));
        return;
    }
    for (std::size_t i = 0; i < wrapped.size(); ++i) {
        Line line;
        if (i == 0) {
            appendSegment(line, marker, markerFg, markerBold);
        } else if (indent > 0) {
            appendSegment(line, std::u32string(static_cast<std::size_t>(indent), U' '), bodyFg, false);
        }
        appendSegment(line, wrapped[i], bodyFg, bodyBold);
        out.push_back(std::move(line));
    }
}

// A two-column table of keys and actions. The key column is as wide as the
// longest key, the rest of the row belongs to the action. In a
// narrow pane the table falls back to one column ("Ctrl+N  New tab").
inline void appendKeyRows(std::vector<Line>& out, const std::vector<KeyRow>& keys, int width,
                          Color keyFg, Color bodyFg) {
    int keyWidth = 0;
    for (const auto& key : keys)
        keyWidth = std::max(keyWidth, static_cast<int>(key.keys.size()));

    const int gap = 2;
    const int actionWidth = width - keyWidth - gap;
    if (actionWidth < 12) {
        for (const auto& key : keys) {
            if (static_cast<int>(key.keys.size()) + 2 >= width) {
                // A long key label cannot fit beside its action; wrap the
                // combined row rather than letting the marker overflow.
                appendMarked(out, {}, text::toUtf32(key.keys + " - " + key.action), width,
                             keyFg, true, bodyFg, false, 0);
            } else {
                appendMarked(out, text::toUtf32(key.keys + "  "), text::toUtf32(key.action),
                             width, keyFg, true, bodyFg, false, 0);
            }
        }
        return;
    }

    for (const auto& key : keys) {
        const std::u32string keyText = text::toUtf32(key.keys);
        const std::vector<std::u32string> wrapped = text::wrap(text::toUtf32(key.action), actionWidth);

        Line first;
        appendSegment(first, keyText, keyFg, true);
        const int pad = keyWidth - static_cast<int>(keyText.size());
        if (pad > 0)
            appendSegment(first, std::u32string(static_cast<std::size_t>(pad), U' '), bodyFg, false);
        appendSegment(first, std::u32string(static_cast<std::size_t>(gap), U' '), bodyFg, false);
        if (wrapped.empty()) {
            out.push_back(std::move(first));
            continue;
        }
        appendSegment(first, wrapped[0], bodyFg, false);
        out.push_back(std::move(first));

        for (std::size_t i = 1; i < wrapped.size(); ++i) {
            Line cont;
            appendSegment(cont, std::u32string(static_cast<std::size_t>(keyWidth + gap), U' '),
                          bodyFg, false);
            appendSegment(cont, wrapped[i], bodyFg, false);
            out.push_back(std::move(cont));
        }
    }
}

// The "see also" links: numbered, so a digit opens the page.
inline void appendLinks(std::vector<Line>& out, const std::vector<std::string>& ids,
                        const Library& library, int width, Color numberFg, Color bodyFg,
                        int& nextNumber) {
    for (const auto& id : ids) {
        const Page* target = library.find(id);
        const std::string label = target ? target->title : id;
        appendMarked(out, text::toUtf32(std::to_string(nextNumber++) + ")  "), text::toUtf32(label),
                     width, numberFg, true, bodyFg, false, 3);
    }
}

// Lays the page out for the given width (in cells) and returns the lines.
// The layout is pure: it only depends on the page, the library (for the link
// titles) and the width.
inline std::vector<Line> layoutPage(const Page& page, const Library& library, int width) {
    std::vector<Line> out;
    const int w = std::max(8, width);
    const auto& t = theme::active;

    // The title is a band at the top of the page.
    Line title;
    title.band = true;
    appendSegment(title, text::clip(text::toUtf32(page.title), w), t.titleText, true);
    out.push_back(std::move(title));

    if (!page.summary.empty()) {
        for (const auto& line : text::wrap(text::toUtf32(page.summary), w))
            out.push_back(textLine(line, t.editorText, false));
    }
    out.push_back(blankLine());

    std::string lastHeading;
    int nextLinkNumber = 1;
    for (const auto& section : page.sections) {
        // The heading is drawn once per group of blocks.
        if (!section.heading.empty() && section.heading != lastHeading) {
            out.push_back(blankLine());
            out.push_back(textLine(text::clip(text::toUtf32(section.heading), w), t.docHeading, true));
            lastHeading = section.heading;
        }

        switch (section.kind) {
            case SectionKind::Text:
                appendMarked(out, {}, text::toUtf32(section.text), w, t.editorText, false,
                             t.editorText, false, 0);
                break;
            case SectionKind::Bullets:
                for (const auto& item : section.items)
                    appendMarked(out, U"- ", text::toUtf32(item), w, t.dimText, false,
                                 t.editorText, false, 2);
                break;
            case SectionKind::Steps:
                for (std::size_t i = 0; i < section.items.size(); ++i) {
                    const std::string number = std::to_string(i + 1) + ". ";
                    appendMarked(out, text::toUtf32(number), text::toUtf32(section.items[i]), w,
                                 t.docKey, true, t.editorText, false, 3);
                }
                break;
            case SectionKind::Keys:
                appendKeyRows(out, section.keys, w, t.docKey, t.editorText);
                break;
            case SectionKind::Note:
                appendMarked(out, U"Note: ", text::toUtf32(section.text), w, t.docHeading, true,
                             t.editorText, false, 6);
                break;
            case SectionKind::SeeAlso:
                appendLinks(out, section.links, library, w, t.docKey, t.editorText,
                            nextLinkNumber);
                break;
        }
        out.push_back(blankLine());
    }

    return out;
}

} // namespace fenrir::apps::handbook
