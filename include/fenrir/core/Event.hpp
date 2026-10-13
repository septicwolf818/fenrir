#pragma once

namespace fenrir {

// KeyEvent (see input/Key.hpp) is handled directly in the Application
// loop - it is the only event source at this stage (keyboard).
// The structs below exist so that adding mouse/touch/timers later won't
// require an architecture rebuild, but they are not yet emitted anywhere.

struct PointerEvent {
    int x = 0, y = 0;
    int button = 0;
    bool pressed = false;
};

struct ResizeEvent {
    int width = 0, height = 0;
};

struct TimerEvent {
    int id = 0;
};

} // namespace fenrir
