#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "fenrir/input/Key.hpp"

namespace fenrir {

// Converts raw bytes from Terminal::read() into KeyEvent.
//
// Escape sequences split across read() calls are reassembled: a trailing
// incomplete sequence (a lone ESC, a partial CSI/SS3, a cut UTF-8 char) is
// held in a pending buffer and prepended to the next feed(). This matters
// for Alt+keys on a TTY (e.g. Alt+Left = ESC ESC [ D — bytes that may
// arrive in two reads over SSH/tmux/latency): without reassembly the lone
// ESC becomes a stray Escape and the rest a plain (unmodified) key.
//
// The holder never waits: use wantsMore() right after feed() to peek for
// the missing bytes with a short timeout, and flush() to release a held
// ESC as a lone Escape when nothing follows. Callers must drain via
// flush() on empty reads as a safety net.
class KeyParser {
public:
    std::vector<KeyEvent> feed(const char* data, std::size_t len);
    std::vector<KeyEvent> flush();
    bool wantsMore() const { return !pending_.empty(); }

private:
    KeyEvent parseEscapeSequence(std::string_view seq) const;
    static std::pair<char32_t, int> decodeUtf8(std::string_view data);
    // Start index of a trailing incomplete sequence in s, or npos.
    static std::size_t incompleteTailStart(std::string_view s);
    void parseComplete(const char* data, std::size_t len, std::vector<KeyEvent>& out);

    std::string pending_;
};

} // namespace fenrir
