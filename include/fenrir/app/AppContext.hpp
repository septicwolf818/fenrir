#pragma once
#include <functional>
#include <string>

namespace fenrir {

// To jest "API open/open folder/save" - jedyny sposob, w jaki aplikacja
// pokazuje uzytkownikowi dialog wyboru pliku/folderu. Implementuje go
// Desktop (patrz desktop/Desktop.hpp); aplikacje dostaja referencje do
// AppContext w App::createView(AppContext&) i NIE wiedza nic wiecej o tym,
// jak dialog jest zaimplementowany (to wciaz zwykle Window+FileDialog,
// ale aplikacja nie musi tego wiedziec - to szczegol frameworka).
//
// Wszystkie trzy metody sa asynchroniczne: otwieraja okno dialogowe i
// wracaja natychmiast; wynik przychodzi pozniej przez onPick (lub wcale,
// jesli uzytkownik anuluje - wtedy wolane jest onCancel, jesli podane).
class AppContext {
public:
    virtual ~AppContext() = default;

    using OnPick = std::function<void(const std::string& path)>;
    using OnCancel = std::function<void()>;

    // Dialog wyboru PLIKU do otwarcia (np. Ctrl+O w edytorze).
    virtual void openFile(const std::string& startDir, OnPick onPick, OnCancel onCancel = {}) = 0;

    // Dialog wyboru FOLDERU (np. "Idz do folderu..." w File Managerze).
    virtual void openFolder(const std::string& startDir, OnPick onPick, OnCancel onCancel = {}) = 0;

    // Dialog zapisu: przegladanie + pole nazwy pliku (np. "Zapisz jako...").
    virtual void saveFile(const std::string& startDir, const std::string& suggestedName,
                           OnPick onPick, OnCancel onCancel = {}) = 0;
};

} // namespace fenrir
