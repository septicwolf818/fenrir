#pragma once
#include <functional>
#include <string>
#include <vector>

#include "fenrir/widgets/Widget.hpp"

namespace fenrir {

// Wielowierszowy edytor tekstu. Buforuje linie jako std::u32string, wiec
// jest od razu gotowy na Unicode. Przewijanie pionowe jest obslugiwane;
// poziomego (dla bardzo dlugich linii) na razie brak - linia po prostu
// ucina sie na granicy widgetu.
class TextArea : public Widget {
public:
    TextArea();

    void loadFile(const std::string& path);
    void saveFile(const std::string& path);
    void saveToCurrentPath(); // zapisuje tam, skad wczytano / dokad zapisano ostatnio

    // Nowe metody dla menu aplikacji.
    void newFile();             // utworz nowy pusty dokument
    void clearText();           // czyści cały tekst (dla Calculator)

    const std::string& path() const { return path_; }
    bool dirty() const { return dirty_; }

    // TextArea jest edytowalny — ustwiamy to w konstruktorze (pole editable_ z klasy bazowej).

    std::function<void(const std::string& message)> onStatus;
    std::function<void()> onSaveRequested; // Ctrl+S
    std::function<void()> onOpenRequested; // Ctrl+O

    void draw(Screen& screen) override;
    KeyEventResult handleKey(const KeyEvent& ev) override;
    std::optional<std::pair<int, int>> cursorPosition() const override;

private:
    std::vector<std::u32string> lines_{std::u32string()};
    int cursorRow_ = 0, cursorCol_ = 0;
    int scrollRow_ = 0;
    std::string path_;
    bool dirty_ = false;

    void insertChar(char32_t ch);
    void insertNewline();
    void backspace();
    void del();
    void moveCursor(int dRow, int dCol);
    void clampCursor();
    void ensureVisible();
};

} // namespace fenrir
