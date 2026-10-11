#pragma once
#include <string>
#include <vector>

#include "fenrir/core/FileSystem.hpp"
#include "fenrir/widgets/ListView.hpp"

namespace fenrir {

// Silnik przegladania katalogu: stan (biezacy katalog, wpisy, blad) + nawigacja
// po katalogach. To NIE jest Widget — model jest komponowany przez DirectoryView
// (i przez to tez przez FileDialog / Menedzer plikow), zeby wszyscy dzielili
// dokladnie ta sama logike listowania zamiast pisac ja kilka razy.
//
// Zaznaczenie/przewijanie zyje w ListView (widget) — ten model jest czysty.
class DirectoryListing {
public:
    explicit DirectoryListing(std::string startDir);

    const std::string& currentDir() const { return dir_; }
    const std::vector<fs::Entry>& entries() const { return entries_; }
    bool hasError() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    bool empty() const { return entries_.empty(); }

    void reload(); // ponowny odczyt biezacego katalogu (np. po operacji na plikach)
    void goUp();   // wejscie do katalogu nadrzednego

    const fs::Entry* at(int idx) const;         // nullptr gdy brak wpisu
    std::string pathAt(int idx) const;          // pelna sciezka wpisu (".." -> rodzic)
    bool isFileAt(int idx) const;               // true = zwykly plik
    bool isParentAt(int idx) const;             // true = wpis ".."
    // Wejscie do katalogu o podanym indeksie.
    // true = wszedl (katalog zostal przeladowany); false = to plik — wywolujacy decyduje.
    bool enterAt(int idx);

    // Elementy ListView z biezacego katalogu — JEDYNE miejsce konwersji nazw
    // plikow na tekst ekranowy (UTF-8 -> UTF-32 przez text::toUtf32, prefiks
    // "/ " dla katalogow — identyczny wyglad jak wczesniej wszedzie).
    std::vector<ListView::Item> items() const;

private:
    std::string dir_;
    std::vector<fs::Entry> entries_;
    std::string error_;

    void loadDir(const std::string& dir);
};

} // namespace fenrir
