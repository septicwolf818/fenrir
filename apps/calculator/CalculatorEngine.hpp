#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace fenrir::apps {

// Czysta logika kalkulatora (bez widgetow, bez ekranu) - latwa do testowania
// i ponownego uzycia. Semantyka klasycznego kalkulatora 4-funkcyjnego:
//  - cyfry / kropka: wpychanie biezacego wpisu (entry),
//  - operator (+ - * /): domyka biezacy wpis do akumulatora (takze łańcuchowo,
//    np. "2 + 3 *" = 5, potem mnozenie),
//  - "=": domyka calosc do wyniku,
//  - C: reset; +/- znak; %: /100; x^2: kwadrat.
// Bledy (dzielenie przez zero, overflow) sa przechwycone i sygnalowane;
// kalkulator czeka na C.
class CalculatorEngine {
public:
    void inputDigit(char d) {
        if (error_) return;
        if (justEvaluated_) resetAfterEquals();
        if (!entering_) { entry_.clear(); entering_ = true; }
        rhsReady_ = true; // swiezy operand = "=" moze liczyc
        // Notacja naukowa (zapis po %/x^2) nie nadaje sie do dopisywania cyfr.
        if (entry_.find_first_of("eE") != std::string::npos) entry_.clear();
        if (entry_ == "0") entry_.clear();
        else if (entry_ == "-0") entry_.erase(1); // "-0" + cyfra -> "-5", nie "-05"
        if (entry_.size() < 16) entry_ += d;
    }

    void inputDot() {
        if (error_) return;
        if (justEvaluated_) resetAfterEquals();
        if (!entering_) { entry_.clear(); entering_ = true; }
        rhsReady_ = true; // jw.
        if (entry_.find_first_of("eE") != std::string::npos) entry_.clear(); // jw.
        if (entry_.find('.') == std::string::npos) entry_ += '.';
    }

    void setOp(char op) {
        if (error_) return;
        double cur = currentEntry();
        const bool repeat = !entering_ && pendingOp_ == op;
        if (entering_) {
            if (pendingOp_) {
                if (!apply(acc_, pendingOp_, cur)) return;
            } else {
                acc_ = cur;
            }
        } else if (repeat) {
            // powtorzenie operatora bez wpisu: "5 + + *" -> 5+5, potem mnozenie
            if (!apply(acc_, pendingOp_, cur)) return;
        }
        // Inny operator bez wpisu = tylko podmiana ("5 + *" -> potem mnozenie).
        // entry_ puste: glowa pokazuje acc_ tak jak po "=".
        entry_.clear();
        pendingOp_ = op;
        entering_ = false;
        justEvaluated_ = false;
        // Po powtorzeniu acc_ juz zawiera wynik — nastepne "=" tylko go
        // potwierdza, nie liczy drugi raz (10+10). Bez powtorzenia "=" moze
        // liczyc z acc_ jako prawym operandem ("5 + =" -> 10).
        rhsReady_ = !repeat;
    }

    void equals() {
        if (error_ || !pendingOp_) return;
        if (rhsReady_) {
            if (!apply(acc_, pendingOp_, currentEntry())) return;
        }
        pendingOp_ = 0;
        entering_ = false;
        entry_.clear(); // w glowie pokazywany jest teraz acc_
        justEvaluated_ = true;
    }

    void clear() {
        acc_ = 0; entry_.clear(); pendingOp_ = 0;
        entering_ = false; justEvaluated_ = false; error_ = false;
        rhsReady_ = true;
    }

    void negate() {
        if (error_) return;
        if (entering_ && !entry_.empty()) {
            if (entry_.rfind('-', 0) == 0) entry_.erase(0, 1);
            else entry_.insert(0, "-");
        } else {
            acc_ = -acc_; // wpis pusty -> glowa pokazuje acc_
        }
        justEvaluated_ = false;
    }

    // % i x^2 dzialaja TYLKO na biezacym wpisie (glowie) i nie ruszaja
    // akumulatora ani oczekujacego operatora - wiec "10 + 20 % " = 20% jest
    // podstawa drugiego operandu, a "2 x^2 + 3 =" = 4 + 3 = 7.
    void percent() {
        if (error_) return;
        double v = currentEntry() / 100.0;
        if (!std::isfinite(v)) { error_ = true; return; }
        entry_ = format(v);
        entering_ = true;
        justEvaluated_ = false;
        rhsReady_ = true; // swiezy operand po przeliczeniu
    }

    void square() {
        if (error_) return;
        double v = currentEntry() * currentEntry();
        if (!std::isfinite(v)) { error_ = true; return; }
        entry_ = format(v);
        entering_ = true;
        justEvaluated_ = false;
        rhsReady_ = true; // jw.
    }

    // Tekst na glowie (right-aligned; widok sam go wyrównuje).
    std::string display() const {
        if (error_) return "Błąd";
        if (entry_.empty()) return format(acc_);
        std::string s = entry_;
        if (s.back() == '.') s.pop_back(); // "2." -> "2"
        if (s.empty()) return "0";         // sam "." -> pokazuj "0"
        return s;
    }

private:
    double acc_ = 0.0;          // akumulator (pierwszy operand / wynik)
    std::string entry_;         // biezacy wpis (cyfry wpychane przez uzytkownika)
    char pendingOp_ = 0;        // oczekujacy operator: + - * /
    bool entering_ = false;     // true = wpychanie entry_
    bool justEvaluated_ = false;// true = dopiero co naciśnięto "="
    bool rhsReady_ = true;      // "=" moze uzyc acc_ jako prawego operandu
    bool error_ = false;

    void resetAfterEquals() {
        acc_ = 0; entry_.clear(); pendingOp_ = 0;
        entering_ = false; justEvaluated_ = false;
    }

    double currentEntry() const {
        if (entry_.empty()) return acc_;
        std::string s = entry_;
        if (s == ".") s = "0.";
        if (s.rfind("-.", 0) == 0) s = "-0.";
        try { return std::stod(s); } catch (...) { return acc_; }
    }

    // Zwraca true = powodzenie; false = blad (acc_ NIE zmienione).
    bool apply(double a, char op, double b) {
        double r = 0;
        switch (op) {
            case '+': r = a + b; break;
            case '-': r = a - b; break;
            case '*': r = a * b; break;
            case '/':
                if (b == 0.0) { error_ = true; return false; }
                r = a / b;
                break;
            default: error_ = true; return false;
        }
        if (!std::isfinite(r)) { error_ = true; return false; }
        acc_ = r;
        return true;
    }

    static std::string format(double v) {
        if (v == 0.0) return "0";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v);
        return buf;
    }
};

} // namespace fenrir::apps
