#pragma once
#include <string_view>
#include <vector>

#include "fenrir/input/Key.hpp"

namespace fenrir {

// Converts raw bytes from Terminal::read() into KeyEvent. Assumes that one
// sequence (e.g. an arrow = 3 bytes ESC [ A) arrives in a single read()
// — true for a local keyboard; over a slow network link the sequence
// could split across several read() calls, which is not handled
// here (a deliberate simplification at this stage).
class KeyParser {
public:
    std::vector<KeyEvent> feed(const char* data, std::size_t len);

private:
    KeyEvent parseEscapeSequence(std::string_view seq) const;
    static std::pair<char32_t, int> decodeUtf8(std::string_view data);
};

} // namespace fenrir
