#include "fenrir/core/Screen.hpp"
#include "fenrir/window/Window.hpp"

#include "test_harness.hpp"

#include <memory>

using namespace fenrir;
using fenrir::test::check;

namespace {

class FocusColorWidget final : public Widget {
public:
    void draw(Screen& screen) override {
        const Color fg = focused_ ? colors::BrightGreen : colors::BrightRed;
        screen.fillRect(bounds_, Cell{U'x', fg, colors::Black, 0});
    }

    KeyEventResult handleKey(const KeyEvent&) override { return KeyEventResult::Ignored; }
};

} // namespace

void runWindowTests() {
    Window window("Focus test", Rect{2, 2, 20, 10}, std::make_unique<FocusColorWidget>());
    Screen screen(40, 20);

    window.setFocused(true);
    window.draw(screen);
    const Cell activeContent = screen.at(5, 5);
    const Cell activeBorder = screen.at(2, 5);

    window.setFocused(false);
    screen.clear();
    window.draw(screen);
    const Cell inactiveContent = screen.at(5, 5);
    const Cell inactiveBorder = screen.at(2, 5);

    check(inactiveContent == activeContent,
          "inactive window preserves its content colors");
    check(inactiveBorder.fg != activeBorder.fg || inactiveBorder.bg != activeBorder.bg,
          "inactive window changes its border colors");
}
