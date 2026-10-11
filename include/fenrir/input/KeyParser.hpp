#pragma once
#include <string_view>
#include <vector>

#include "fenrir/input/Key.hpp"

namespace fenrir {

// Zamienia surowe bajty z Terminal::read() na KeyEvent. Zakłada, że jedna
// sekwencja (np. strzałka = 3 bajty ESC [ A) przychodzi w jednym read()
// — prawdziwe dla lokalnej klawiatury; przy wejściu przez wolne łącze
// sieciowe sekwencja mogłaby się rozdzielić na kilka read(), co tu nie
// jest obsługiwane (celowa uproszczenie na tym etapie).
class KeyParser {
public:
    std::vector<KeyEvent> feed(const char* data, std::size_t len);

private:
    KeyEvent parseEscapeSequence(std::string_view seq) const;
    static std::pair<char32_t, int> decodeUtf8(std::string_view data);
};

} // namespace fenrir
