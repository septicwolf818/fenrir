#pragma once
#include <vector>

#include "fenrir/core/Screen.hpp"
#include "fenrir/terminal/Terminal.hpp"

namespace fenrir {

// Renderer keeps its own copy of the last screen content sent
// ("front buffer") and on every present() sends to the terminal ONLY
// the cells that changed since the previous frame.
class Renderer {
public:
    explicit Renderer(Terminal& terminal);

    void present(const Screen& screen);
    void forceFullRedraw(); // e.g. after a terminal resize

private:
    Terminal& terminal_;
    std::vector<Cell> previous_;
    int prevWidth_ = -1;
    int prevHeight_ = -1;
    bool fullRedraw_ = true;
};

} // namespace fenrir
