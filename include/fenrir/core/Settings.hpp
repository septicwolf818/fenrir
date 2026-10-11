#pragma once
#include <string>

namespace fenrir::settings {

// Ustawienia pulpitu, trwale zapisywane w ~/.config/fenrir/settings.ini
// (prosty format key=value). Desktop laduje je przy starcie i stosuje:
// motyw (theme::apply), nazwe pulpitu, zegar na pasku zadan, migajacy kursor.
// Aplikacja Ustawienia zmienia te wartosci i zapisuje — kolejny start
// przywraca wybor uzytkownika.
struct Settings {
    std::string theme = "classic"; // id z theme::catalog()
    bool taskbarSeconds = false;   // zegar na pasku zadan ze sekundami
    std::string desktopName = "Fenrir"; // nazwa w pasku menu (prawa strona)
    bool cursorBlink = true;       // migajacy kursor GUI
};

// Biezace ustawienia (ladowane raz przez Desktop przy starcie).
inline Settings active{};

// Sciezka pliku ustawien: $HOME/.config/fenrir/settings.ini
std::string path();

// Wczytuje ustawienia z pliku; brak pliku = wartosci domyslne.
// Nieznane klucze/blersze wiersze sa ignorowane; nieznane id motywu
// wraca do "classic".
void load();

// Zapisuje biezace ustawienia do pliku (tworzy katalogi rodzicielskie).
void save();

} // namespace fenrir::settings
