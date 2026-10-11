#pragma once
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fenrir/core/Screen.hpp"
#include "fenrir/input/Key.hpp"

namespace fenrir {

// Pojedynczy element menu (podpasek). Linkuje etykiete do akcji.
struct MenuItem {
    std::u32string label;      // tekst widoczny w menu
    std::function<void()> cb;   // akcja po wybraniu
};

// Grupa elementów — pasek górny (np. "Plik", "Edycja").
using MenuTab = std::pair<std::u32string, std::vector<MenuItem>>;

// Sekwencja klawiszy skrótu w aplikacji. Pojedyncze wciśnięcie.
struct KeyShortcut {
    bool ctrl = false;
    Key key = Key::Unknown;
    std::optional<char32_t> character = std::nullopt; // opcjonalny znak (np. U'C' dla Ctrl+C)
};

// Result zwracany przez handleKey — Desktop używa go do decydowania o trybie.
enum class KeyEventResult {
    Ignored,      // żadne dziecko nie chce tego klawisza
    Consumed,     // obsłużony, bez zmiany trybu (np. nawigacja w kontenerze)
    EnteredEdit,  // start edycji — przejscie do trybu Edit na tym widgete
    Activated,    // aktywowano podległy element (np. przycisk przez Enter/Space)
};

// Bazowa klasa wszystkich widgetów. Widget zna tylko swój prostokąt
// (współrzędne WZGLĘDEM ekranu, nie względem okna — Window przelicza to
// przy setBounds, żeby sam widget nie musiał nic wiedzieć o oknie).
class Widget {
public:
    virtual ~Widget() = default;

    void setBounds(const Rect& r) { bounds_ = r; }
    const Rect& bounds() const { return bounds_; }

    // focused_ = czy to okno (lub widget w oknie) jest aktywnym fokusowanym elementem.
    // highlighted_ = czy ten widget jest aktualnie "podświetlony" (np. wybrany w nawigacji).
    void setFocused(bool f) { focused_ = f; }
    bool focused() const { return focused_; }
    void setHighlighted(bool h) { highlighted_ = h; }
    bool highlighted() const { return highlighted_; }

    // Czy ten widget akceptuje tekst bezpośrednio (edytowalny)? true = wciśnięcie znaku
    // powoduje przejście do trybu Edit na tym widgetze. Domyślnie false.
    bool editable() const { return editable_; }
    void setEditable(bool e) { editable_ = e; }

    // --- Menu / shortcuts API (używane przez Desktop przy renderowaniu i skróty) ---

    // Wszystkie karty menu tej aplikacji (zapisywane przez createView() w Widget).
    const std::vector<MenuTab>& menuTabs() const { return menuTabs_; }
    void setMenuTabs(std::vector<MenuTab> tabs) { menuTabs_ = std::move(tabs); }

    // Skróty klawiszowe tej aplikacji.
    const std::vector<std::pair<KeyShortcut, std::function<void()>>>& shortcuts() const { return shortcuts_; }
    void setShortcuts(std::vector<std::pair<KeyShortcut, std::function<void()>>> s) { shortcuts_ = std::move(s); }

    // Dodaje pojedynczy skrót (używane przez createView()).
    void pushShortcut(KeyShortcut sc, std::function<void()> cb) {
        shortcuts_.push_back({std::move(sc), std::move(cb)});
    }

    virtual void draw(Screen& screen) = 0;

    // Zwraca KeyEventResult — Desktop używa go do decydowania o trybie.
    virtual KeyEventResult handleKey(const KeyEvent& ev) = 0;

    // Absolutna pozycja kursora GUI (w komórkach ekranu), jeśli widget
    // ma sens edycyjny (np. TextArea). std::nullopt = brak własnego kursora.
    virtual std::optional<std::pair<int, int>> cursorPosition() const { return std::nullopt; }

protected:
    Rect bounds_{};
    bool focused_ = false;
    bool highlighted_ = false;
    bool editable_ = true;

    // Metadane aplikacji — ustawiane przez createView() w Widget.
    std::vector<MenuTab> menuTabs_;
    std::vector<std::pair<KeyShortcut, std::function<void()>>> shortcuts_;
};

} // namespace fenrir
