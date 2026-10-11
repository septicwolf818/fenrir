#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace fenrir::text {

// Jedyne miejsce w projekcie kodujace/dekodujace UTF-8. Wszystkie konwersje
// std::string <-> std::u32string ida przez te funkcje (zadnego `u32(begin,end)`,
// ktore traktuje bajty jako znaki i psuje Polskie znaki).

inline void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

inline std::string toUtf8(std::u32string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t cp : s) appendUtf8(out, cp);
    return out;
}

// Dekoduje UTF-8 -> UTF-32. Bledna/niepelna sekwencje traktujemy jak pojedynczy
// bajt (nie gubimy danych przy wczytywaniu plikow).
inline std::u32string toUtf32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    std::size_t i = 0;
    const std::size_t n = s.size();
    auto cont = [&](std::size_t idx) -> int {
        if (idx >= n) return -1;
        unsigned char b = static_cast<unsigned char>(s[idx]);
        if ((b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
    };
    while (i < n) {
        unsigned char b0 = static_cast<unsigned char>(s[i]);
        if (b0 < 0x80) {
            out += static_cast<char32_t>(b0);
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            int c1 = cont(i + 1);
            if (c1 < 0) { out += static_cast<char32_t>(b0); i += 1; continue; }
            out += static_cast<char32_t>(((b0 & 0x1F) << 6) | c1);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            int c1 = cont(i + 1), c2 = cont(i + 2);
            if (c1 < 0 || c2 < 0) { out += static_cast<char32_t>(b0); i += 1; continue; }
            out += static_cast<char32_t>(((b0 & 0x0F) << 12) | (c1 << 6) | c2);
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            int c1 = cont(i + 1), c2 = cont(i + 2), c3 = cont(i + 3);
            if (c1 < 0 || c2 < 0 || c3 < 0) { out += static_cast<char32_t>(b0); i += 1; continue; }
            out += static_cast<char32_t>(((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3);
            i += 4;
        } else {
            out += static_cast<char32_t>(b0);
            i += 1;
        }
    }
    return out;
}

// Ucina tekst do `width` komorek. Jesli obcielo, dokleja `ellipsis` (domyślnie "...").
inline std::u32string truncate(std::u32string_view s, int width, std::u32string_view ellipsis = U"...") {
    if (width <= 0) return {};
    if (static_cast<int>(s.size()) <= width) return std::u32string(s);
    int keep = width - static_cast<int>(ellipsis.size());
    if (keep < 0) {
        // Miejsca mniej niz sam ellipsis — uciamy na znak.
        return std::u32string(s.substr(0, static_cast<std::size_t>(width)));
    }
    return std::u32string(s.substr(0, static_cast<std::size_t>(keep))) + std::u32string(ellipsis);
}

// Przycina bez ellipsis — do wypelniania prostokatow tekstem.
inline std::u32string clip(std::u32string_view s, int width) {
    if (width <= 0) return {};
    if (static_cast<int>(s.size()) <= width) return std::u32string(s);
    return std::u32string(s.substr(0, static_cast<std::size_t>(width)));
}

// Zlam tekstu slowami do podanej szerokosci (dialogi, komunikaty).
// Slowo dluzsze niz szerokosc lamie sie twardo.
inline std::vector<std::u32string> wrap(std::u32string_view s, int width) {
    std::vector<std::u32string> out;
    if (width <= 0) return out;

    std::u32string line;
    std::size_t i = 0;
    const std::size_t n = s.size();

    auto hardBreak = [&](std::u32string& word) {
        while (static_cast<int>(word.size()) > width) {
            out.push_back(word.substr(0, static_cast<std::size_t>(width)));
            word.erase(0, static_cast<std::size_t>(width));
        }
    };

    while (i < n) {
        const std::size_t start = i;
        while (i < n && s[i] != U' ') ++i;
        std::u32string word(s.substr(start, i - start));
        while (i < n && s[i] == U' ') ++i;

        if (line.empty()) {
            hardBreak(word);
            line = std::move(word);
        } else if (static_cast<int>(line.size()) + 1 + static_cast<int>(word.size()) <= width) {
            line += U' ';
            line += word;
        } else {
            out.push_back(std::move(line));
            line.clear();
            hardBreak(word);
            line = std::move(word);
        }
    }
    if (!line.empty() || out.empty()) out.push_back(std::move(line));
    return out;
}

} // namespace fenrir::text
