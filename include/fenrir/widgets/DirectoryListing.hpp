#pragma once
#include <optional>
#include <string>
#include <vector>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/core/Screen.hpp"

namespace fenrir {

// Silnik przegladania katalogu: stan (biezacy katalog, wpisy, zaznaczenie,
// przewijanie) + nawigacja. To NIE jest Widget - jest komponowany PRZEZ
// widgety (FileDialog, FileManagerView), zeby obie te klasy dzielily
// dokladnie ta sama logike listowania/nawigacji zamiast pisac ja dwa razy.
class DirectoryListing {
public:
    explicit DirectoryListing(std::string startDir);

    const std::string& currentDir() const { return dir_; }
    const std::vector<fs::Entry>& entries() const { return entries_; } // ".." jest entries_[0] gdy obecne
    int selectedIndex() const { return selected_; }
    bool hasError() const { return !error_.empty(); }
    const std::string& error() const { return error_; }

    void moveSelection(int delta);
    void reload(); // ponowny odczyt biezacego katalogu (np. po operacji na plikach)
    void goUp();   // wejscie do katalogu nadrzednego, niezaleznie od biezacego zaznaczenia

    // true = zaznaczenie bylo katalogiem/".." i nawigacja sie odbyla (entries_ zostaly przeladowane).
    // false = zaznaczenie to zwykly plik - wywolujacy decyduje co z tym zrobic.
    bool activateSelected();

    bool selectedIsFile() const;
    std::string selectedFullPath() const; // dir_ + nazwa zaznaczonego wpisu (dziala tez dla katalogow)

    void ensureVisible(int viewportHeight);
    int scrollOffset() const { return scroll_; }

private:
    std::string dir_;
    std::vector<fs::Entry> entries_;
    int selected_ = 0;
    int scroll_ = 0;
    std::string error_;

    void loadDir(const std::string& dir);
};

// Rysuje liste (naglowek ze sciezka + przewijana lista wpisow z podswietleniem
// zaznaczenia) w podanym prostokacie. Uzywane identycznie przez FileDialog
// i FileManagerView.
void drawDirectoryListing(Screen& screen, const Rect& area, DirectoryListing& listing);

} // namespace fenrir
