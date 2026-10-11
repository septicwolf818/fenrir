#pragma once
#include <vector>

#include "fenrir/core/Screen.hpp"
#include "fenrir/terminal/Terminal.hpp"

namespace fenrir {

// Renderer trzyma własną kopię ostatnio wysłanej zawartości ekranu
// ("front buffer") i przy każdym present() wysyła do terminala TYLKO
// komórki, które się zmieniły od poprzedniej klatki.
class Renderer {
public:
    explicit Renderer(Terminal& terminal);

    void present(const Screen& screen);
    void forceFullRedraw(); // np. po zmianie rozmiaru terminala

private:
    Terminal& terminal_;
    std::vector<Cell> previous_;
    int prevWidth_ = -1;
    int prevHeight_ = -1;
    bool fullRedraw_ = true;
};

} // namespace fenrir
