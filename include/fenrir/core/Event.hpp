#pragma once

namespace fenrir {

// KeyEvent (patrz input/Key.hpp) jest obsługiwane bezpośrednio w pętli
// Application - to jedyne źródło zdarzeń na tym etapie (klawiatura).
// Poniższe struktury istnieją, żeby dodanie myszy/dotyku/timerów w
// przyszłości nie wymagało przebudowy architektury, ale nie są jeszcze
// nigdzie emitowane.

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
