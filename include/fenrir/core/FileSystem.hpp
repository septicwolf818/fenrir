#pragma once
#include <string>
#include <vector>

namespace fenrir::fs {

struct Entry {
    std::string name;
    bool isDir = false;
};

// Listuje zawartosc katalogu: katalogi pierwsze (alfabetycznie), potem
// pliki (alfabetycznie). NIE zawiera syntetycznego ".." - o to dba
// DirectoryListing (patrz widgets/DirectoryListing.hpp), bo tylko on wie,
// czy w danym kontekscie wejscie "w gore" ma sens pokazywac.
// Rzuca std::runtime_error przy bledzie (brak dostepu, katalog nie istnieje).
std::vector<Entry> list(const std::string& dir);

bool isDirectory(const std::string& path);
bool exists(const std::string& path);

// Katalog nadrzedny; dla "/" zwraca "/" (nie ma wyzej).
std::string parent(const std::string& dir);

// Sciezka bezwzgledna (wzgledem cwd, znormalizowana). Katalogi startowe
// paneli MUSZA byc bezwzgledne — dla sciezek wzglednych (".", "foo")
// parent() zwraca siebie same i nawigacja "w gore" sie zapada.
std::string absolute(const std::string& path);

// Sklejenie katalogu i nazwy wpisu w poprawna sciezke (bez podwojnych '/').
std::string join(const std::string& dir, const std::string& name);

// Ostatni element sciezki (nazwa pliku/katalogu). "" dla sciezek konczacych sie '/'.
std::string nameOf(const std::string& path);

// Kopiuje zawartosc pliku bajt w bajt. Rzuca std::runtime_error przy bledzie.
void copyFile(const std::string& from, const std::string& to);

// Kopiuje PLIK albo KATALOG (rekurencyjnie). Rzuca std::runtime_error.
void copyPath(const std::string& from, const std::string& to);

// Przenosi/nazwa zmienia (rename) plik lub katalog. Rzuca std::runtime_error.
void renamePath(const std::string& from, const std::string& to);

// Usuwa plik albo katalog (rekurencyjnie). Rzuca std::runtime_error.
void removePath(const std::string& path);

// Rozmiar pliku w bajtach, albo -1 jesli nie da sie odczytac.
long long fileSize(const std::string& path);

// Usuwa pojedynczy plik. NIE obsluzuje katalogow. Rzuca błąd przy problemach.
void removeFile(const std::string& path);

// $HOME z otoczenia (dziala i jako 'pi' na Raspberry Pi OS, i jako root
// w kontenerze testowym); "/" jako ostateczny fallback.
std::string homeDir();

} // namespace fenrir::fs
