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
// for Alt+keys on a TTY (Alt+PgUp = ESC ESC [ 5 ~ — five bytes that may
// arrive in two reads over SSH/tmux/latency): without reassembly the lone
// ESC becomes a stray Escape and the rest a plain (unmodified) key.
//
// A held ESC is only released by flush(), which Desktop calls when a read
// times out (no more bytes are coming imminently): a genuinely lone Escape
// then arrives at most one idle tick late, while split sequences always
// reassemble correctly. Callers must pump flush() on empty reads.
class KeyParser {
public:
    std::vector<KeyEvent> feed(const char* data, std::size_t len);
    std::vector<KeyEvent> flush();

private:
    KeyEvent parseEscapeSequence(std::string_view seq) const;
    static std::pair<char32_t, int> decodeUtf8(std::string_view data);
    // Start index of a trailing incomplete sequence in s, or npos.
    static std::size_t incompleteTailStart(std::string_view s);
    void parseComplete(const char* data, std::size_t len, std::vector<KeyEvent>& out);

    std::string pending_;
};

} // namespace fenrir
