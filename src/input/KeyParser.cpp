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
        // CSI <ps>;<mods>(A|B|C|D|H|F) — zmodyfikowane strzalki, np. ESC[1;5A
        // = Ctrl+Up (xterm i wiekszosc emulatorow wysyla to bez potrzeby
        // modifyOtherKeys). Uzywane m.in. do Ctrl+Up/Down = rok w kalendarzu.
        if (seq.size() >= 4) {
            const char last = seq.back();
            if (last == 'A' || last == 'B' || last == 'C' || last == 'D' ||
                last == 'H' || last == 'F') {
                const auto semi = seq.find(';');
                if (semi != std::string_view::npos) {
                    int mods = -1;
                    if (semi + 1 < seq.size() - 1) {
                        mods = 0;
                        for (std::size_t k = semi + 1; k + 1 < seq.size(); ++k) {
                            const char c = seq[k];
                            if (c < '0' || c > '9') { mods = -1; break; }
                            mods = mods * 10 + (c - '0');
                            if (mods > 99) { mods = -1; break; }
                        }
                    }
                    if (mods >= 0) {
                        ev.ctrl = (mods & 4) != 0;
                        ev.alt = (mods & 2) != 0;
                        switch (last) {
                            case 'A': ev.key = Key::Up; return ev;
                            case 'B': ev.key = Key::Down; return ev;
                            case 'C': ev.key = Key::Right; return ev;
                            case 'D': ev.key = Key::Left; return ev;
                            case 'H': ev.key = Key::Home; return ev;
                            case 'F': ev.key = Key::End; return ev;
                        }
                    }
                }
            }
        }
        // sekwencje CSI zakończone '~', np. ESC[3~ (Delete), ESC[5~ (PgUp)
        if (seq.back() == '~') {
            std::string_view num = seq.substr(1, seq.size() - 2);
            // Parsujemy cyfry recznie (string_view), bez <string> — na
            // libstdc++ nie wolno liczyc na transisywne dosylki naglowkow.
            auto digits = [](std::string_view s) -> int {
                if (s.empty()) return -1;
                int v = 0;
                for (char c : s) {
                    if (c < '0' || c > '9') return -1;
                    v = v * 10 + (c - '0');
                    if (v > 99) return -1; // sekwencje CSI sa krotkie
                }
                return v;
            };
            // modifyOtherKeys / CSI-u: ESC[27;<mods>;<code>~ (np. 27;5;9~ =
            // Ctrl+Tab). Dostarczaja to emulatory i keymapy obslugujace
            // modyfikatory; na golym TTY (Linux VC) taka sekwencja po prostu
            // sie nie pojawia i ta sciezka jest martwa — ale kosztuje zero.
            if (num.size() > 3 && num.substr(0, 2) == "27") {
                auto s1 = num.find(';');
                auto s2 = (s1 == std::string_view::npos) ? std::string_view::npos
                                                         : num.find(';', s1 + 1);
                if (s1 != std::string_view::npos && s2 != std::string_view::npos) {
                    const int mods = digits(num.substr(s1 + 1, s2 - s1 - 1));
                    const int code = digits(num.substr(s2 + 1));
                    if (mods >= 0 && code >= 0) {
                        ev.ctrl = (mods & 4) != 0;
                        ev.alt = (mods & 2) != 0;
                        switch (code) {
                            case 9:  ev.key = Key::Tab; return ev;
                            case 13: ev.key = Key::Enter; return ev;
                            case 27: ev.key = Key::Escape; return ev;
                            case 32: ev.key = Key::Char; ev.character = U' '; return ev;
                            default: break;
                        }
                    }
                }
            }
            // Zmodyfikowane klawisze '~': ESC[5;5~ = Ctrl+PgUp, ESC[6;5~ =
            // Ctrl+PgDn (xterm i wiekszosc emulatorow dosyla tak PgUp/PgDn/
            // Home/End z modyfikatorem). Bez modyfikatora num jest czysto
            // numeryczny i ten blok sie nie wlacza.
            std::string_view code = num;
            const auto msemi = num.find(';');
            if (msemi != std::string_view::npos) {
                const int mods = digits(num.substr(msemi + 1));
                if (mods >= 0) {
                    ev.ctrl = (mods & 4) != 0;
                    ev.alt = (mods & 2) != 0;
                    code = num.substr(0, msemi);
                } else {
                    return ev; // nieznana kombinacja — nie udajemy znanych klawiszy
                }
            }
            if (code == "1" || code == "7") ev.key = Key::Home;
            else if (code == "3") ev.key = Key::Delete;
            else if (code == "4" || code == "8") ev.key = Key::End;
            else if (code == "5") ev.key = Key::PageUp;
            else if (code == "6") ev.key = Key::PageDown;
            else if (code == "11") ev.key = Key::F1;
            else if (code == "12") ev.key = Key::F2;
            else if (code == "13") ev.key = Key::F3;
            else if (code == "14") ev.key = Key::F4;
            else if (code == "15") ev.key = Key::F5;
            else if (code == "17") ev.key = Key::F6;
            else if (code == "18") ev.key = Key::F7;
            else if (code == "19") ev.key = Key::F8;
            else if (code == "20") ev.key = Key::F9;
            else if (code == "21") ev.key = Key::F10;
            else if (code == "23") ev.key = Key::F11;
            else if (code == "24") ev.key = Key::F12;
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
            // Konsola Linux: Alt + klawisz wielobajtowy = ESC prefiksowany
            // przed CAŁĄ sekwencją (np. Alt+PgUp = 1b 1b 5b 35 7e, bo Prior
            // i tak zaczyna się od ESC). Zjadamy powtorzone ESC i pamietamy
            // alt dla zdarzenia z sekwencji ponizej.
            bool altPref = false;
            while (i + 1 < len && data[i + 1] == 0x1b) { altPref = true; ++i; }

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
                if (altPref) ev.alt = true;
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
                    break; // c to bajt końcowy sekwencji (litera albo '~')
                }
                std::string_view seq(data + i + 1, j - (i + 1));
                KeyEvent ev = parseEscapeSequence(seq);
                if (altPref) ev.alt = true;
                out.push_back(ev);
                i = j;
            } else if (i + 1 < len) {
                // ESC + znak = Alt+znak. Konsola Linux (VC_META domyslnie
                // wl.) wysyla wlasnie tak Alt+X — np. Alt+Tab = ESC 0x09.
                // (Po zjedzeniu prefiksow z gory data[i+1] juz nigdy nie jest
                // ESC, wiec ten oddzial nie lapsuje podwojnych ESC.)
                unsigned char b2 = static_cast<unsigned char>(data[i + 1]);
                KeyEvent ev;
                ev.alt = true;
                std::size_t adv = 2;
                if (b2 == '\r' || b2 == '\n') {
                    ev.key = Key::Enter;
                } else if (b2 == '\t') {
                    ev.key = Key::Tab;
                } else if (b2 == 0x7f || b2 == 0x08) {
                    ev.key = Key::Backspace;
                } else if (b2 < 0x20) {
                    ev.key = Key::Char;
                    ev.ctrl = true;
                    ev.character = static_cast<char32_t>(b2 + 0x60); // Alt+Ctrl+litera
                } else {
                    auto [cp, consumed] = decodeUtf8(std::string_view(data + i + 1, len - (i + 1)));
                    if (consumed <= 0) consumed = 1;
                    ev.key = Key::Char;
                    ev.character = cp;
                    adv = 1 + static_cast<std::size_t>(consumed);
                }
                out.push_back(ev);
                i += adv;
            } else {
                KeyEvent ev{Key::Escape, 0, false};
                if (altPref) ev.alt = true; // np. sam ESC ESC = alt+Escape
                out.push_back(ev);
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
