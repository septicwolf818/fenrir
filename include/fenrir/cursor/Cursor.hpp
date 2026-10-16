#pragma once
#include "fenrir/core/Screen.hpp"

namespace fenrir {

// Custom GUI cursor. Does NOT use the terminal's system cursor (that one is
// permanently hidden by Terminal). render() is called AFTER everything else
// has drawn to Screen — it stamps a solid high-contrast block over (x,y).
// This is an overlay: it does not modify the persistent
// Screen contents (Screen is cleared and redrawn from scratch every frame).
class Cursor {
public:
    int x() const { return x_; }
    int y() const { return y_; }

    void moveTo(int x, int y) { x_ = x; y_ = y; }
    void moveBy(int dx, int dy) { x_ += dx; y_ += dy; }
    void clamp(int width, int height);

    bool visible() const { return visible_; }
    void setVisible(bool v) { visible_ = v; }

    void render(Screen& screen) const;

private:
    int x_ = 0, y_ = 0;
    bool visible_ = true;
};

} // namespace fenrir
