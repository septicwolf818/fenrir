#pragma once
#include <functional>
#include <string>
#include <vector>

#include "fenrir/app/AppContext.hpp"
#include "fenrir/widgets/DirectoryListing.hpp"
#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Glowny widok File Managera w formie DWA PANELI.
// Lewy i prawy panel = osobny DirectoryListing (kazdy z własnym katalogiem).
// Aktywny panel nawigowany strzalkami; operacje (kopiuj/wklej/usun) dzialaja
// na zaznaczonym elemencie z aktywnego panelu.

class FileManagerView : public Widget {
public:
    FileManagerView(AppContext& ctx, std::string startDir);

    std::function<void(const std::string& message)> onStatus;

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& ev) override;

    // --- Metody publiczne wywolywane przez menu aplikacji --------------------
    
    void goUp();                      // idz do folderu nadrzednego w aktywnym panelu
    void pasteToOtherPane();          // wklej ze schowka do przeciwnego panelu
    void renameSelected();            // otworzy dialog nazwy (przez AppContext)
    void deleteSelected();            // usunie zaznaczony element z aktywnego panelu
    void refreshBoth();               // odswiezy oba panele
    void goToFolder();                // otwiera dialog wyboru folderu w aktywnym panelu

    // --- Dostep do stanu paneli (dla menu) -----------------------------------
    const DirectoryListing& leftPane() const { return left_; }
    const DirectoryListing& rightPane() const { return right_; }
    bool isRightActive() const { return rightActive_; }

private:
    AppContext& ctx_;

    // Dwa panele katalogow.
    DirectoryListing left_{"."};
    DirectoryListing right_{"."};
    bool rightActive_ = false;  // true = prawy panel aktywny, false = lewy

    // Schowek operacyjny: sciezki do skopiowania/przeniesienia/usuniecia.
    std::vector<std::string> clipboard_;
    enum class ClipboardOp { Copy, Cut } clipOp_ = ClipboardOp::Copy;

    void status(const std::string& msg);

    // Operacje na plikach.
    bool copyClipboardTo(DirectoryListing& dest, std::string& errMsg);
    bool moveClipboardTo(DirectoryListing& dest, std::string& errMsg);
    bool deleteClipboardItems(std::string& errMsg);

    // Pomocnicze — rysowanie jednego panelu.
    static void drawPanel(Screen& screen, const Rect& area, DirectoryListing& listing);
};

} // namespace fenrir
