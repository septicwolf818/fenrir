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

inline void highlightLine(Language language, std::u32string_view line,
                          std::vector<SyntaxSpan>& spans) {
    spans.clear();
    if (language == Language::None || line.empty()) return;

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

    for (std::size_t i = 0; i < line.size();) {
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

inline TextArea::SyntaxHighlighter forPath(const std::string& path) {
    const Language language = languageForPath(path);
    if (language == Language::None) return {};
    return [language](std::u32string_view line, std::vector<SyntaxSpan>& spans) {
        highlightLine(language, line, spans);
    };
}

} // namespace fenrir::apps::syntax
