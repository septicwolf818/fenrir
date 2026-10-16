#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "fenrir/widgets/TextArea.hpp"

namespace fenrir::apps::syntax {

enum class Language { None, CLike, Python, Shell, Json, Markup, Css, Ini, Yaml, Markdown, CMake };

inline Language languageForPath(const std::string& path) {
    const std::filesystem::path filePath(path);
    std::string extension = filePath.extension().string();
    std::string filename = filePath.filename().string();
    const auto lowercase = [](std::string& text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    };
    lowercase(extension);
    lowercase(filename);
    if (filename == "cmakelists.txt" || extension == ".cmake") return Language::CMake;
    if (filename == "makefile" || filename == "gnumakefile") return Language::Shell;
    if (extension == ".toml" || extension == ".properties") return Language::Ini;
    if (extension == ".c" || extension == ".h" || extension == ".cc" ||
        extension == ".cpp" || extension == ".cxx" || extension == ".hpp" ||
        extension == ".hxx" || extension == ".js" || extension == ".mjs" ||
        extension == ".cjs" || extension == ".ts" || extension == ".tsx" ||
        extension == ".java" || extension == ".go" || extension == ".rs")
        return Language::CLike;
    if (extension == ".py") return Language::Python;
    if (extension == ".sh" || extension == ".bash" || extension == ".zsh") return Language::Shell;
    if (extension == ".json") return Language::Json;
    if (extension == ".html" || extension == ".htm" || extension == ".xml") return Language::Markup;
    if (extension == ".css") return Language::Css;
    if (extension == ".ini" || extension == ".cfg" || extension == ".conf") return Language::Ini;
    if (extension == ".yaml" || extension == ".yml") return Language::Yaml;
    if (extension == ".md" || extension == ".markdown") return Language::Markdown;
    return Language::None;
}

inline bool isSpace(char32_t c) { return c == U' ' || c == U'\t'; }
inline bool isDigit(char32_t c) { return c >= U'0' && c <= U'9'; }
inline bool isWordStart(char32_t c) {
    return (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_' || c == U'$';
}
inline bool isWordPart(char32_t c) { return isWordStart(c) || isDigit(c); }

inline std::string asciiWord(std::u32string_view word) {
    std::string result;
    result.reserve(word.size());
    for (char32_t c : word) {
        if (c > 0x7f) return {};
        result.push_back(static_cast<char>(c));
    }
    return result;
}

inline bool inWords(std::string_view words, const std::string& word) {
    std::size_t pos = 0;
    while (pos < words.size()) {
        const std::size_t end = words.find(' ', pos);
        if (words.substr(pos, end == std::string_view::npos ? end : end - pos) == word) return true;
        if (end == std::string_view::npos) break;
        pos = end + 1;
    }
    return false;
}

inline SyntaxToken wordToken(Language language, const std::string& word) {
    static constexpr std::string_view types =
        "int float double char bool string String str byte void object Object "
        "size_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t int64_t "
        "Vec Vec2 Vec3 Vec4 List Map Set Result Option Error File Path";
    static constexpr std::string_view common =
        "if else for while do switch case break continue return try catch finally throw "
        "class struct enum union typedef using namespace public private protected static "
        "const constexpr volatile inline virtual override template typename signed unsigned "
        "new delete this true false null nullptr import from as export default extends "
        "implements interface package function async await yield let var fn impl trait mut "
        "pub use mod crate self super match where loop in of";
    static constexpr std::string_view python =
        "and as assert async await break class continue def del elif else except finally "
        "for from global if import in is lambda nonlocal not or pass raise return try "
        "while with yield True False None";
    static constexpr std::string_view shell =
        "if then elif else fi for in do done case esac while until function select "
        "return export local readonly unset source alias break continue";
    static constexpr std::string_view json = "true false null";
    static constexpr std::string_view css =
        "important inherit initial unset none block inline flex grid absolute relative "
        "fixed auto transparent solid hidden bold italic";
    static constexpr std::string_view yaml = "true false null yes no on off";
    static constexpr std::string_view cmake =
        "cmake_minimum_required project add_library add_executable target_link_libraries "
        "target_include_directories target_sources if endif elseif else set option find_package "
        "include install message foreach endforeach function endfunction return file "
        "configure_file add_custom_command add_custom_target public private interface on off";

    if (inWords(types, word)) return SyntaxToken::Type;
    if (language == Language::Python && inWords(python, word)) return SyntaxToken::Keyword;
    if (language == Language::Shell && inWords(shell, word)) return SyntaxToken::Keyword;
    if (language == Language::Json && inWords(json, word)) return SyntaxToken::Keyword;
    if (language == Language::Css && inWords(css, word)) return SyntaxToken::Keyword;
    if (language == Language::Yaml && inWords(yaml, word)) return SyntaxToken::Keyword;
    if (language == Language::CMake) {
        std::string lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (inWords(cmake, lower)) return SyntaxToken::Keyword;
    }
    if ((language == Language::CLike || language == Language::Markup) && inWords(common, word))
        return SyntaxToken::Keyword;
    return SyntaxToken::Identifier;
}

inline void addSpan(std::vector<SyntaxSpan>& spans, std::size_t begin, std::size_t end,
                    SyntaxToken token) {
    if (end > begin) spans.push_back({begin, end, token});
}

inline bool hasBlockComment(Language language) {
    return language == Language::CLike || language == Language::Css ||
           language == Language::Markup || language == Language::CMake;
}

// Closer for a continued block comment: */ (C-like, CSS), --> (markup),
// ]] (CMake #[[ ... ]]).
inline std::u32string_view blockCloser(Language language) {
    if (language == Language::Markup) return U"-->";
    if (language == Language::CMake) return U"]]";
    return U"*/";
}

inline void highlightLine(Language language, std::u32string_view line,
                          bool startsInBlockComment,
                          std::vector<SyntaxSpan>& spans) {
    spans.clear();
    if (language == Language::None || line.empty()) return;

    // Continuation of a block comment opened on an earlier line: comment up
    // to the matching closer (or to end of line), then lex the rest normally.
    std::size_t i = 0;
    if (startsInBlockComment && hasBlockComment(language)) {
        const std::u32string_view close = blockCloser(language);
        const std::size_t found = line.find(close);
        const std::size_t end =
            found == std::u32string_view::npos ? line.size() : found + close.size();
        addSpan(spans, 0, end, SyntaxToken::Comment);
        if (found == std::u32string_view::npos) return;
        i = end;
    }

    std::size_t first = 0;
    while (first < line.size() && isSpace(line[first])) ++first;
    if (language == Language::Markdown && first < line.size() && line[first] == U'#') {
        addSpan(spans, first, line.size(), SyntaxToken::Keyword);
        return;
    }
    if (language == Language::Ini) {
        if (first < line.size() && (line[first] == U'#' || line[first] == U';')) {
            addSpan(spans, first, line.size(), SyntaxToken::Comment);
            return;
        }
        if (first < line.size() && line[first] == U'[') {
            addSpan(spans, first, line.size(), SyntaxToken::Type);
            return;
        }
    }

    for (; i < line.size();) {
        if ((language == Language::CLike || language == Language::Css) &&
            i + 1 < line.size() && line[i] == U'/' && line[i + 1] == U'/') {
            if (language == Language::CLike) {
                addSpan(spans, i, line.size(), SyntaxToken::Comment);
                break;
            }
        }
        if ((language == Language::CLike || language == Language::Css) &&
            i + 1 < line.size() && line[i] == U'/' && line[i + 1] == U'*') {
            const std::size_t close = line.find(U"*/", i + 2);
            const std::size_t end = close == std::u32string_view::npos ? line.size() : close + 2;
            addSpan(spans, i, end, SyntaxToken::Comment);
            i = end;
            continue;
        }
        if ((language == Language::Python || language == Language::Shell ||
             language == Language::CMake ||
             language == Language::Yaml || language == Language::Ini) && line[i] == U'#') {
            addSpan(spans, i, line.size(), SyntaxToken::Comment);
            break;
        }
        if (language == Language::Ini && line[i] == U';') {
            addSpan(spans, i, line.size(), SyntaxToken::Comment);
            break;
        }
        if (language == Language::Markup && i + 3 < line.size() &&
            line.substr(i, 4) == U"<!--") {
            const std::size_t close = line.find(U"-->", i + 4);
            const std::size_t end = close == std::u32string_view::npos ? line.size() : close + 3;
            addSpan(spans, i, end, SyntaxToken::Comment);
            i = end;
            continue;
        }
        if (language == Language::CLike && i == first && line[i] == U'#') {
            addSpan(spans, i, line.size(), SyntaxToken::Directive);
            break;
        }
        if (language == Language::Markup && line[i] == U'<') {
            const std::size_t end = line.find(U'>', i + 1);
            const std::size_t tagEnd = end == std::u32string_view::npos ? line.size() : end + 1;
            std::size_t name = i + 1;
            if (name < tagEnd && line[name] == U'/') ++name;
            std::size_t nameEnd = name;
            while (nameEnd < tagEnd && isWordPart(line[nameEnd])) ++nameEnd;
            addSpan(spans, name, nameEnd, SyntaxToken::Keyword);
            i = nameEnd;
            continue;
        }
        if (line[i] == U'\'' || line[i] == U'"' ||
            ((language == Language::CLike || language == Language::Markdown) && line[i] == U'`')) {
            const char32_t quote = line[i];
            const std::size_t begin = i++;
            while (i < line.size()) {
                if (line[i] == U'\\' && i + 1 < line.size()) { i += 2; continue; }
                if (line[i++] == quote) break;
            }
            SyntaxToken token = SyntaxToken::String;
            if (language == Language::Json) {
                std::size_t next = i;
                while (next < line.size() && isSpace(line[next])) ++next;
                if (next < line.size() && line[next] == U':') token = SyntaxToken::Type;
            }
            addSpan(spans, begin, i, token);
            continue;
        }
        if (isDigit(line[i])) {
            const std::size_t begin = i++;
            while (i < line.size() && (isWordPart(line[i]) || line[i] == U'.')) ++i;
            addSpan(spans, begin, i, SyntaxToken::Number);
            continue;
        }
        if (isWordStart(line[i])) {
            const std::size_t begin = i++;
            while (i < line.size() && isWordPart(line[i])) ++i;
            const std::string word = asciiWord(line.substr(begin, i - begin));
            if (!word.empty()) {
                std::size_t next = i;
                while (next < line.size() && isSpace(line[next])) ++next;
                if ((language == Language::Ini && next < line.size() && line[next] == U'=') ||
                    (language == Language::Yaml && next < line.size() && line[next] == U':') ||
                    (language == Language::Css && next < line.size() && line[next] == U':'))
                    addSpan(spans, begin, i, SyntaxToken::Type);
                else {
                    const SyntaxToken token = wordToken(language, word);
                    if (token != SyntaxToken::Identifier) addSpan(spans, begin, i, token);
                }
            }
            continue;
        }
        ++i;
    }
}

inline bool startsWithAt(std::u32string_view s, std::size_t i, std::u32string_view pre) {
    return s.size() - i >= pre.size() && s.substr(i, pre.size()) == pre;
}

// Whole-buffer block-comment scan: result[i] is nonzero when logical line i
// STARTS inside a block comment. Mirrors highlightLine's rules so per-line
// spans and start states always agree:
//  - strings ('...', "...", plus `...` in CLike) with backslash escapes never
//    open or close comments (a Markup <!-- is still an opener — highlightLine
//    checks it before strings, and so do we; a <!-- inside a tag attribute
//    is swallowed by highlightLine's <...> tag branch, so the scan runs
//    the same tag-name parse instead of treating < as plain text),
//  - // line comments (CLike) end the line's scan,
//  - a CLike line whose first non-space character is '#' is a preprocessor
//    directive (atomic — highlightLine lexes it whole, so it never opens),
//  - CMake '#' opens a bracket comment only as #[[ ... ]] (closed by ]]);
//    any other '#' is a plain line comment.
inline std::vector<char> blockCommentStates(Language language,
                                            const std::vector<std::u32string>& lines) {
    std::u32string_view open, close;
    switch (language) {
        case Language::CLike:
        case Language::Css: open = U"/*"; close = U"*/"; break;
        case Language::Markup: open = U"<!--"; close = U"-->"; break;
        case Language::CMake: open = U"#[["; close = U"]]"; break;
        default: return {}; // no block comments — TextArea keeps no cache
    }
    const bool backtick = (language == Language::CLike);
    const bool slashComment = (language == Language::CLike);

    std::vector<char> out(lines.size(), 0);
    bool inComment = false;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::u32string_view ln(lines[li]);
        out[li] = inComment ? 1 : 0;

        std::size_t i = 0;
        if (!inComment && language == Language::CLike) {
            while (i < ln.size() && isSpace(ln[i])) ++i;
            if (i < ln.size() && ln[i] == U'#') continue; // directive: atomic line
        }
        while (i < ln.size()) {
            if (inComment) {
                const std::size_t found = ln.find(close, i);
                if (found == std::u32string_view::npos) break; // rest of line commented
                i = found + close.size();
                inComment = false;
                continue;
            }
            if (slashComment && startsWithAt(ln, i, U"//")) break; // line comment
            if (startsWithAt(ln, i, open)) {
                inComment = true;
                i += open.size();
                continue;
            }
            if (language == Language::CMake && ln[i] == U'#') break; // plain # comment
            const char32_t c = ln[i];
            if (language == Language::Markup && c == U'<' && !startsWithAt(ln, i, open)) {
                // Mirror highlightLine's tag branch exactly (including its
                // naive '>' search): consume only the tag name so a <!--,
                // quote, or word after it is lexed the same way per-line.
                const std::size_t found = ln.find(U'>', i + 1);
                const std::size_t tagEnd =
                    found == std::u32string_view::npos ? ln.size() : found + 1;
                std::size_t name = i + 1;
                if (name < tagEnd && ln[name] == U'/') ++name;
                while (name < tagEnd && isWordPart(ln[name])) ++name;
                i = name;
                continue;
            }
            if (c == U'\'' || c == U'"' || (backtick && c == U'`')) {
                ++i;
                while (i < ln.size()) {
                    if (ln[i] == U'\\' && i + 1 < ln.size()) { i += 2; continue; }
                    if (ln[i++] == c) break;
                }
                continue;
            }
            ++i;
        }
    }
    return out;
}

// Both halves of highlighting for one file: the per-line lexer plus the
// whole-buffer block-comment scan (empty when the language has no block
// comments). TextArea keeps them in sync (cache rebuilt after every edit).
struct Highlighter {
    TextArea::SyntaxHighlighter line;
    TextArea::BlockCommentScan blocks;
};

inline Highlighter forPath(const std::string& path) {
    const Language language = languageForPath(path);
    if (language == Language::None) return {};
    Highlighter result;
    result.line = [language](std::u32string_view line, bool inBlock,
                             std::vector<SyntaxSpan>& spans) {
        highlightLine(language, line, inBlock, spans);
    };
    if (hasBlockComment(language)) {
        result.blocks = [language](const std::vector<std::u32string>& lines) {
            return blockCommentStates(language, lines);
        };
    }
    return result;
}

} // namespace fenrir::apps::syntax
