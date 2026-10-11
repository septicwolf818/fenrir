#pragma once
#include "fenrir/core/Screen.hpp"

namespace fenrir {

// Własny kursor GUI. NIE używa systemowego kursora terminala (ten jest
// trwale ukryty przez Terminal). render() jest wywoływane PO tym, jak
// wszystko inne narysowało się do Screen — bierze komórkę pod (x,y) i
// zamienia miejscami jej fg/bg. To overlay: nie modyfikuje trwałej
// zawartości Screen (bo Screen jest czyszczony i rysowany od nowa co klatkę).
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
