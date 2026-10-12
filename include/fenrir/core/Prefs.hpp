#pragma once
#include <map>
#include <string>
#include <string_view>

namespace fenrir::prefs {

// Preferencje aplikacji — JEDEN plik key=value na aplikacje:
//   ~/.config/fenrir/apps/<appId>.conf
// (katalog $HOME/.config/fenrir istnieje juz dzieki settings.ini; podkatalog
// apps/ Prefs tworzy przy pierwszym zapisie). Format jak w settings.ini:
// `klucz=wartosc`, linie z '#'/';' i puste sa ignorowane. Nieznane klucze
// przy odczycie trafiaja do mapy — aplikacja moze je ignorowac, wiec
// dodanie/odjecie klucza jest kompatybilne w obu kierunkach.
//
// Uzycie w aplikacji:
//   auto& p = prefs::forApp("editor");   // cache'owana instancja na proces
//   bool spaces = p.getBool("insert_spaces", false);
//   p.setBool("insert_spaces", true);    // zapisuje plik od razu
//
// set* zapisuje plik natychmiast (male pliki — brak dirty-trackingu, aplikacje
// nie musza pamietac o save()). save() tez jest publiczne (zbiorcze zapisy).
class Prefs {
public:
    // Wczytuje plik aplikacji o danym appId (brak pliku = pusta mapa).
    void load(std::string_view appId);

    // Zapisuje cala mape do ~/.config/fenrir/apps/<appId>.conf
    // (tworzy katalogi). Zwraca false, gdy pliku nie da sie zapisac.
    bool save() const;

    std::string get(const std::string& key, const std::string& fallback = {}) const;
    bool getBool(const std::string& key, bool fallback = false) const;
    int getInt(const std::string& key, int fallback = 0) const;

    // Ustawia i zapisuje plik. set(key, "") usuwa klucz (plik bez smieci).
    void set(const std::string& key, const std::string& value);
    void setBool(const std::string& key, bool value);
    void setInt(const std::string& key, int value);

    std::string_view appId() const { return appId_; }
    // Pelna sciezka pliku (do komunikatow/debugu).
    std::string path() const;

private:
    std::string appId_;
    std::map<std::string, std::string> kv_;
};

// Instancja preferencji danej aplikacji, cache'owana na caly proces —
// wszystkie okna tej samej aplikacji dziela jedna mape (zapis = jeden plik).
// appId powinno byc stabilne i po angielsku ("editor", "calendar", "paint").
Prefs& forApp(std::string_view appId);

} // namespace fenrir::prefs
