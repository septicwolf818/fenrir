#include "fenrir/input/KeyParser.hpp"

namespace fenrir {

std::pair<char32_t, int> KeyParser::decodeUtf8(std::string_view data) {
    if (data.empty()) return {0, 0};
    unsigned char b0 = static_cast<unsigned char>(data[0]);

    auto cont = [&](int i) -> int {
        if (i >= static_cast<int>(data.size())) return -1;
        unsigned char b = static_cast<unsigned char>(data[i]);
        if ((b & 0xC0) != 0x80) return -1;
        return b & 0x3F;
    };

    if (b0 < 0x80) return {b0, 1};

    if ((b0 & 0xE0) == 0xC0) {
        int c1 = cont(1);
        if (c1 < 0) return {b0, 1};
        char32_t cp = ((b0 & 0x1F) << 6) | c1;
        return {cp, 2};
    }
    if ((b0 & 0xF0) == 0xE0) {
        int c1 = cont(1), c2 = cont(2);
        if (c1 < 0 || c2 < 0) return {b0, 1};
        char32_t cp = ((b0 & 0x0F) << 12) | (c1 << 6) | c2;
        return {cp, 3};
    }
    if ((b0 & 0xF8) == 0xF0) {
        int c1 = cont(1), c2 = cont(2), c3 = cont(3);
        if (c1 < 0 || c2 < 0 || c3 < 0) return {b0, 1};
        char32_t cp = ((b0 & 0x07) << 18) | (c1 << 12) | (c2 << 6) | c3;
        return {cp, 4};
    }
    return {b0, 1}; // niepoprawny UTF-8 - traktujemy bajt jak jest
}

KeyEvent KeyParser::parseEscapeSequence(std::string_view seq) const {
    // seq nie zawiera wiodącego ESC (0x1b) - jest już zdjęty przez feed().
    KeyEvent ev;
    if (seq.empty()) { ev.key = Key::Escape; return ev; } // sam ESC

    if (seq[0] == '[' || seq[0] == 'O') {
        if (seq.size() >= 2) {
            char c = seq[1];
            switch (c) {
                case 'A': ev.key = Key::Up; return ev;
                case 'B': ev.key = Key::Down; return ev;
                case 'C': ev.key = Key::Right; return ev;
                case 'D': ev.key = Key::Left; return ev;
                case 'H': ev.key = Key::Home; return ev;
                case 'F': ev.key = Key::End; return ev;
                case 'P': ev.key = Key::F1; return ev;
                case 'Q': ev.key = Key::F2; return ev;
                case 'R': ev.key = Key::F3; return ev;
                case 'S': ev.key = Key::F4; return ev;
            }
        }
        // sekwencje CSI zakończone '~', np. ESC[3~ (Delete), ESC[5~ (PgUp)
        if (seq.back() == '~') {
            std::string_view num = seq.substr(1, seq.size() - 2);
            if (num == "1" || num == "7") ev.key = Key::Home;
            else if (num == "3") ev.key = Key::Delete;
            else if (num == "4" || num == "8") ev.key = Key::End;
            else if (num == "5") ev.key = Key::PageUp;
            else if (num == "6") ev.key = Key::PageDown;
            else if (num == "11") ev.key = Key::F1;
            else if (num == "12") ev.key = Key::F2;
            else if (num == "13") ev.key = Key::F3;
            else if (num == "14") ev.key = Key::F4;
            else if (num == "15") ev.key = Key::F5;
            else if (num == "17") ev.key = Key::F6;
            else if (num == "18") ev.key = Key::F7;
            else if (num == "19") ev.key = Key::F8;
            else if (num == "20") ev.key = Key::F9;
            else if (num == "21") ev.key = Key::F10;
            else if (num == "23") ev.key = Key::F11;
            else if (num == "24") ev.key = Key::F12;
            return ev;
        }
    }
    ev.key = Key::Unknown;
    return ev;
}

std::vector<KeyEvent> KeyParser::feed(const char* data, std::size_t len) {
    std::vector<KeyEvent> out;
    std::size_t i = 0;
    while (i < len) {
        unsigned char b = static_cast<unsigned char>(data[i]);

        if (b == 0x1b) { // ESC - może być samym Escape albo początkiem sekwencji CSI/SS3
            // Konsola Linux (TERM=linux, gola konsola bez X) wysyła F1-F5 jako
            // ESC [ [ <litera>, czyli z DODATKOWYM '[' i bez cyfr/';' w środku.
            // To nie mieści się w ogólnym schemacie "cyfry/';' aż do bajtu
            // koncowego" ponizej (ten drugi '[' zostalby błędnie uznany za
            // bajt koncowy), więc obsługujemy to jako specjalny przypadek.
            if (i + 3 < len && data[i + 1] == '[' && data[i + 2] == '[') {
                char c = data[i + 3];
                KeyEvent ev;
                switch (c) {
                    case 'A': ev.key = Key::F1; break;
                    case 'B': ev.key = Key::F2; break;
                    case 'C': ev.key = Key::F3; break;
                    case 'D': ev.key = Key::F4; break;
                    case 'E': ev.key = Key::F5; break;
                    default: ev.key = Key::Unknown; break;
                }
                out.push_back(ev);
                i += 4;
                continue;
            }

            if (i + 1 < len && (data[i + 1] == '[' || data[i + 1] == 'O')) {
                std::size_t j = i + 2;
                while (j < len) {
                    unsigned char c = static_cast<unsigned char>(data[j]);
                    ++j;
                    if ((c >= '0' && c <= '9') || c == ';') continue;
                    break; // c to bajt kończący sekwencję (litera albo '~')
                }
                std::string_view seq(data + i + 1, j - (i + 1));
                out.push_back(parseEscapeSequence(seq));
                i = j;
            } else {
                out.push_back(KeyEvent{Key::Escape, 0, false});
                ++i;
            }
            continue;
        }

        if (b == '\r' || b == '\n') { out.push_back(KeyEvent{Key::Enter, 0, false}); ++i; continue; }
        if (b == '\t') { out.push_back(KeyEvent{Key::Tab, 0, false}); ++i; continue; }
        if (b == 0x7f || b == 0x08) { out.push_back(KeyEvent{Key::Backspace, 0, false}); ++i; continue; }

        if (b < 0x20) { // pozostałe znaki kontrolne -> Ctrl+litera
            KeyEvent ev;
            ev.key = Key::Char;
            ev.ctrl = true;
            ev.character = static_cast<char32_t>(b + 0x60); // Ctrl+A(1)->'a' itd.
            out.push_back(ev);
            ++i;
            continue;
        }

        auto [cp, consumed] = decodeUtf8(std::string_view(data + i, len - i));
        if (consumed <= 0) consumed = 1;
        out.push_back(KeyEvent{Key::Char, cp, false});
        i += static_cast<std::size_t>(consumed);
    }
    return out;
}

} // namespace fenrir
