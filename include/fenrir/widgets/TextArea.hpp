#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Wielowierszowy edytor tekstu z OWIJANIEM LINII (word wrap). Bufor trzyma
// LOGICZNE linie (std::u32string, pelne Unicode); layout zamienia je na
// WIERSZE WIZUALNE o szerokosci bounds_.w — lamane na ostatnim slowie w oknie,
// twardy lam dla slow dluzszych niz ekran. Kursor i przewijanie operuja na
// wierszach wizualnych, wiec dlugie linie sa w pelni widoczne i edytowalne.
class TextArea : public Widget {
public:
    TextArea();

    void loadFile(const std::string& path);
    void saveFile(const std::string& path);
    void saveToCurrentPath(); // zapisuje tam, skad wczytano / dokad zapisano ostatnio

    void newFile();   // utworz nowy pusty dokument
    void clearText(); // czyści cały tekst

    // Wstawianie przy Tab: znak tabulatora ('\t', domyslnie) albo spacje
    // (odstep do kolejnej kolumny 4). Ustawiane z preferencji aplikacji
    // edytora; ktorykolwiek tryb — Tab JEST wstawiany (nie przelacza okien).
    void setInsertsSpaces(bool on) { insertsSpaces_ = on; }
    bool insertsSpaces() const { return insertsSpaces_; }

    // Numery linii (gutter po lewej) — przelaczane z menu/shortcutu.
    void setShowLineNumbers(bool on) {
        if (lineNumbers_ == on) return;
        lineNumbers_ = on;
        markDirty();
    }
    bool showLineNumbers() const { return lineNumbers_; }

    const std::string& path() const { return path_; }
    bool dirty() const override { return dirty_; }

    // Pozycja kursora: (linia, kolumna), obie 1-based — dla paska statusu.
    std::pair<int, int> cursorLineCol() const { return {cursorRow_ + 1, cursorCol_ + 1}; }

    // OWIJANIE LINII — domyslnie wlaczone. Wylaczone = dlugie linie leca poza
    // ekran i sa przewijane POZIOMO (scrollCol_ tak jak w LineEdit), wiec
    // kursor i cala tresc sa osiagalne tez na wąskim oknie.
    bool wrap() const { return wrap_; }
    void setWrap(bool on) {
        if (wrap_ == on) return;
        wrap_ = on;
        scrollCol_ = 0;
        markDirty();
        ensureVisible();
    }
    void toggleWrap() {
        setWrap(!wrap_);
        if (onStatus) onStatus(wrap_ ? "Zawijanie linii: wlaczone" : "Zawijanie linii: wylaczone - przewijanie poziome");
    }

    std::function<void(const std::string& message)> onStatus;
    std::function<void()> onSaveRequested; // Ctrl+S
    std::function<void()> onOpenRequested; // Ctrl+O

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& ev) override;
    std::optional<std::pair<int, int>> cursorPosition() const override;

private:
    // Jeden wiersz wizualny: fragment linii logicznej `line` od kolumny `col0`
    // o dlugosci `len`. Segmenty sa ciagle — kazdy znak linii wystepuje
    // dokladnie raz w layoutcie (spacja lamiaca zostaje na koncu wiersza),
    // wiec mapowanie kolumna logica <-> wizualna jest trywialne.
    struct Seg {
        int line;
        int col0;
        int len;
    };

    std::vector<std::u32string> lines_{std::u32string()};
    mutable std::vector<Seg> layout_;
    // Stan layoutu "mutable": cursorPosition() jest const, a layout liczymy
    // leniwie (tez przy odczycie pozycji kursora).
    mutable int layoutW_ = 0; // szerokosc, dla ktorej zbudowano layout
    mutable bool layoutDirty_ = true;

    std::string path_;
    bool dirty_ = false;

    int cursorRow_ = 0, cursorCol_ = 0; // pozycja kursora w LINII LOGICZNEJ
    int scrollRow_ = 0;                 // pierwszy widoczny wiersz WIZUALNY
    int scrollCol_ = 0;                 // pierwsza widoczna kolumna (tylko gdy brak owijania)
    bool wrap_ = true;                  // owijanie linii (domyslnie wl.)
    bool insertsSpaces_ = false;        // Tab wstawia spacje zamiast '\t'
    bool lineNumbers_ = false;          // gutter z numerami linii
    int goalCol_ = 0; // "lepka" kolumna wizualna dla ruchu góra/dół (PageUp itp.)

    void markDirty() { layoutDirty_ = true; }
    void ensureLayout() const;
    void rebuildLayout() const;
    int gutterW() const;               // szerokosc guttera z numerami linii (0 = brak)
    int visualRowOf(int row, int col) const; // index w layout_ dla (row,col)
    std::pair<int, int> logicalOf(int segIdx, int offset) const;
    int visualOffset() const; // biezacy kursor -> offset w jego wierszu wizualnym
    void syncGoal();          // po ruchu poziomym: goal = aktualny offset wizualny

    void insertChar(char32_t ch);
    void insertNewline();
    void backspace();
    void del();
    void moveHoriz(int dCol); // prawo/lewo z przechodzeniem miedzy liniami
    void moveVert(int dSeg);  // góra/dol o wiersz(e) wizualne (lepki cel)
    void clampCursor();
    void ensureVisible();
};

} // namespace fenrir
