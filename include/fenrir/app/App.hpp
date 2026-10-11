#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// App = abstrakcja programu na pulpicie: nazwa, widok, preferowane okno.
// Menu i skróty pochodzą Z WIDGETU ZAWARTOŚCI (Widget::menuTabs / shortcuts),
// który zostaje ustawiony przez createView() (setMenuTabs/pushShortcut) —
// dlatego App nie ma własnych menuTabs()/shortcuts().

class App {
public:
    virtual ~App() = default;

    // Nazwa widoczna w pasku menu / menu aplikacji.
    virtual std::string name() const = 0;

    // Tworzy NOWY widok (stan) aplikacji - wywolywane przy kazdym uruchomieniu,
    // wiec mozna miec kilka niezaleznych okien tej samej aplikacji naraz.
    virtual std::unique_ptr<Widget> createView(AppContext& ctx) = 0;

    // Preferowany rozmiar/pozycja startowa okna wzgledem obszaru pulpitu.
    virtual Rect preferredBounds(const Rect& desktopArea) const {
        int w = std::min(60, std::max(20, desktopArea.w - 4));
        int h = std::min(20, std::max(6, desktopArea.h - 4));
        return Rect{desktopArea.x + 2, desktopArea.y + 1, w, h};
    }
};

} // namespace fenrir
