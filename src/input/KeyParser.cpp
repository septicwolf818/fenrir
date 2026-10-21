#include "fenrir/input/KeyParser.hpp"

#include <string>

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
    return {b0, 1}; // invalid UTF-8 - treat the byte as-is
}

KeyEvent KeyParser::parseEscapeSequence(std::string_view seq) const {
    // seq does not include the leading ESC (0x1b) - it was already stripped by feed().
    KeyEvent ev;
    if (seq.empty()) { ev.key = Key::Escape; return ev; } // bare ESC

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
        // CSI <ps>;<mods>(A|B|C|D|H|F) — modified arrows, e.g. ESC[1;5A
        // = Ctrl+Up (xterm and most emulators send this without needing
        // modifyOtherKeys). Used among others for Ctrl+Up/Down = year in the calendar.
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
                        // The modifier parameter is 1 + a bit mask (1=shift, 2=alt,
                        // 4=ctrl) — ESC[1;2A is Shift+Up, ESC[1;5A is Ctrl+Up.
                        const int bits = mods > 0 ? mods - 1 : 0;
                        ev.ctrl = (bits & 4) != 0;
                        ev.alt = (bits & 2) != 0;
                        ev.shift = (bits & 1) != 0;
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
        // CSI sequences ending in '~', e.g. ESC[3~ (Delete), ESC[5~ (PgUp)
        if (seq.back() == '~') {
            std::string_view num = seq.substr(1, seq.size() - 2);
            // We parse digits manually (string_view), without <string> — on
            // libstdc++ we must not rely on transitive header includes.
            auto digits = [](std::string_view s) -> int {
                if (s.empty()) return -1;
                int v = 0;
                for (char c : s) {
                    if (c < '0' || c > '9') return -1;
                    v = v * 10 + (c - '0');
                    if (v > 99) return -1; // CSI sequences are short
                }
                return v;
            };
            // modifyOtherKeys / CSI-u: ESC[27;<mods>;<code>~ (e.g. 27;5;9~ =
            // Ctrl+Tab). Supplied by emulators and keymaps that support
            // modifiers; on a bare TTY (Linux VC) such a sequence simply
            // never appears and this path is dead — but it costs zero.
            if (num.size() > 3 && num.substr(0, 2) == "27") {
                auto s1 = num.find(';');
                auto s2 = (s1 == std::string_view::npos) ? std::string_view::npos
                                                         : num.find(';', s1 + 1);
                if (s1 != std::string_view::npos && s2 != std::string_view::npos) {
                    const int mods = digits(num.substr(s1 + 1, s2 - s1 - 1));
                    const int code = digits(num.substr(s2 + 1));
                    if (mods >= 0 && code >= 0) {
                        // The modifier parameter is 1 + a bit mask (1=shift, 2=alt,
                        // 4=ctrl) — ESC[1;2A is Shift+Up, ESC[1;5A is Ctrl+Up.
                        const int bits = mods > 0 ? mods - 1 : 0;
                        ev.ctrl = (bits & 4) != 0;
                        ev.alt = (bits & 2) != 0;
                        ev.shift = (bits & 1) != 0;
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
            // Modified '~' keys: ESC[5;5~ = Ctrl+PgUp, ESC[6;5~ =
            // Ctrl+PgDn (xterm and most emulators likewise send PgUp/PgDn/
            // Home/End with a modifier). Without a modifier num is purely
            // numeric and this block does not engage.
            std::string_view code = num;
            const auto msemi = num.find(';');
            if (msemi != std::string_view::npos) {
                const int mods = digits(num.substr(msemi + 1));
                if (mods >= 0) {
                    // The modifier parameter is 1 + a bit mask (1=shift, 2=alt,
                    // 4=ctrl) — ESC[1;2A is Shift+Up, ESC[1;5A is Ctrl+Up.
                    const int bits = mods > 0 ? mods - 1 : 0;
                    ev.ctrl = (bits & 4) != 0;
                    ev.alt = (bits & 2) != 0;
                    ev.shift = (bits & 1) != 0;
                    code = num.substr(0, msemi);
                } else {
                    return ev; // unknown combination — don't fake known keys
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

std::size_t KeyParser::incompleteTailStart(std::string_view s) {
    const std::size_t n = s.size();
    if (n == 0) return std::string_view::npos;

    auto utf8Need = [](unsigned char lead) -> std::size_t {
        if ((lead & 0xE0) == 0xC0) return 2;
        if ((lead & 0xF0) == 0xE0) return 3;
        if ((lead & 0xF8) == 0xF0) return 4;
        return 0;
    };

    // Trailing incomplete UTF-8 (a cut multi-byte char, possibly Alt-prefixed).
    std::size_t lead = n;
    while (lead > 0 && (static_cast<unsigned char>(s[lead - 1]) & 0xC0) == 0x80) --lead;
    if (lead < n) {
        const std::size_t need = utf8Need(static_cast<unsigned char>(s[lead]));
        if (need > 0 && n - lead < need) {
            std::size_t start = lead;
            if (start > 0 && static_cast<unsigned char>(s[start - 1]) == 0x1b) --start;
            return start;
        }
    } else {
        const std::size_t need = utf8Need(static_cast<unsigned char>(s[n - 1]));
        if (need > 0) {
            std::size_t start = n - 1;
            if (start > 0 && static_cast<unsigned char>(s[start - 1]) == 0x1b) --start;
            return start;
        }
    }

    // Trailing incomplete escape sequence: the last ESC whose suffix has no
    // final byte yet. Leading ESCs absorbed as Alt-prefixes belong to the
    // stashed tail as well (e.g. the first half of a split Alt+PgUp).
    for (std::size_t p = n; p-- > 0;) {
        if (s[p] != 0x1b) continue;
        std::size_t start = p;
        while (start > 0 && s[start - 1] == 0x1b) --start;
        std::size_t k = p + 1;
        while (k < n && s[k] == 0x1b) ++k; // swallowed Alt-prefixes
        if (k >= n) return start;          // trailing ESC(s)
        const char c1 = s[k];
        if (c1 == '[') {
            std::size_t m = k + 1;
            if (m < n && s[m] == '[') ++m; // Linux console [[ F-key intro
            while (m < n && ((s[m] >= '0' && s[m] <= '9') || s[m] == ';')) ++m;
            if (m >= n) return start; // parameters but no final byte yet
            break;                    // complete sequence — stop looking left
        }
        if (c1 == 'O') {
            if (k + 1 >= n) return start; // SS3 intro without its final byte
            break;
        }
        // ESC + regular char = complete Alt+char; earlier bytes are settled.
    }
    return std::string_view::npos;
}

std::vector<KeyEvent> KeyParser::feed(const char* data, std::size_t len) {
    std::vector<KeyEvent> out;
    std::string buf;
    buf.reserve(pending_.size() + len);
    buf += pending_;
    buf.append(data, len);
    pending_.clear();

    std::size_t cut = buf.size();
    const std::size_t tail = incompleteTailStart(buf);
    if (tail != std::string_view::npos) {
        pending_ = buf.substr(tail);
        cut = tail;
    }
    parseComplete(buf.data(), cut, out);
    return out;
}

std::vector<KeyEvent> KeyParser::flush() {
    std::vector<KeyEvent> out;
    if (pending_.empty()) return out;
    // No continuation arrived (read timed out): the held bytes stand alone.
    // A lone ESC becomes Escape; a partial CSI parses best-effort (Unknown,
    // ignored downstream) — same as before reassembly existed.
    std::string rest = std::move(pending_);
    pending_.clear();
    parseComplete(rest.data(), rest.size(), out);
    return out;
}

void KeyParser::parseComplete(const char* data, std::size_t len, std::vector<KeyEvent>& out) {
    std::size_t i = 0;
    while (i < len) {
        unsigned char b = static_cast<unsigned char>(data[i]);

        if (b == 0x1b) { // ESC - may be a lone Escape or the start of a CSI/SS3 sequence
            // Linux console: Alt + multi-byte key = ESC prefixed
            // before the WHOLE sequence (e.g. Alt+PgUp = 1b 1b 5b 35 7e, since Prior
            // itself starts with ESC). We swallow the repeated ESC and remember
            // alt for the event from the sequence below.
            bool altPref = false;
            while (i + 1 < len && data[i + 1] == 0x1b) { altPref = true; ++i; }

            // The Linux console (TERM=linux, bare console without X) sends F1-F5 as
            // ESC [ [ <letter>, i.e. with an EXTRA '[' and no digits/';' inside.
            // This does not fit the general "digits/';' up to the final byte"
            // scheme below (the second '[' would be wrongly taken as the
            // final byte), so we handle it as a special case.
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
                    break; // c is the sequence's final byte (letter or '~')
                }
                std::string_view seq(data + i + 1, j - (i + 1));
                KeyEvent ev = parseEscapeSequence(seq);
                if (altPref) ev.alt = true;
                out.push_back(ev);
                i = j;
            } else if (i + 1 < len) {
                // ESC + char = Alt+char. The Linux console (VC_META on by
                // default) sends Alt+X exactly this way - e.g. Alt+Tab = ESC 0x09.
                // (After swallowing the prefixes above data[i+1] is never
                // ESC anymore, so this branch never swallows doubled ESCs.)
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
                    ev.character = static_cast<char32_t>(b2 + 0x60); // Alt+Ctrl+letter
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
                if (altPref) ev.alt = true; // e.g. bare ESC ESC = alt+Escape
                out.push_back(ev);
                ++i;
            }
            continue;
        }

        if (b == '\r' || b == '\n') { out.push_back(KeyEvent{Key::Enter, 0, false}); ++i; continue; }
        if (b == '\t') { out.push_back(KeyEvent{Key::Tab, 0, false}); ++i; continue; }
        if (b == 0x7f || b == 0x08) { out.push_back(KeyEvent{Key::Backspace, 0, false}); ++i; continue; }

        if (b < 0x20) { // other control characters -> Ctrl+letter
            KeyEvent ev;
            ev.key = Key::Char;
            ev.ctrl = true;
            ev.character = static_cast<char32_t>(b + 0x60); // Ctrl+A(1)->'a' etc.
            out.push_back(ev);
            ++i;
            continue;
        }

        auto [cp, consumed] = decodeUtf8(std::string_view(data + i, len - i));
        if (consumed <= 0) consumed = 1;
        out.push_back(KeyEvent{Key::Char, cp, false});
        i += static_cast<std::size_t>(consumed);
    }
}

} // namespace fenrir
