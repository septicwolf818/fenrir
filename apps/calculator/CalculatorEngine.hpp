#pragma once
#include <cmath>
#include <cstdio>
#include <string>

namespace fenrir::apps {

// Pure calculator logic (no widgets, no screen) - easy to test and
// reuse. Semantics of a classic four-function calculator:
//  - digits / dot: push into the current entry,
//  - operator (+ - * /): folds the current entry into the accumulator (also
//    chained, e.g. "2 + 3 *" = 5, then multiplication),
//  - "=": folds everything into the result,
//  - C: reset; +/- sign; %: /100; x^2: square.
// Errors (division by zero, overflow) are caught and flagged;
// the calculator waits for C.
class CalculatorEngine {
public:
    void inputDigit(char d) {
        if (error_) return;
        if (justEvaluated_) resetAfterEquals();
        if (!entering_) { entry_.clear(); entering_ = true; }
        rhsReady_ = true; // fresh operand = "=" can compute
        // Scientific notation (from %/x^2) can't have digits appended to it.
        if (entry_.find_first_of("eE") != std::string::npos) entry_.clear();
        if (entry_ == "0") entry_.clear();
        else if (entry_ == "-0") entry_.erase(1); // "-0" + digit -> "-5", not "-05"
        if (entry_.size() < 16) entry_ += d;
    }

    void inputDot() {
        if (error_) return;
        if (justEvaluated_) resetAfterEquals();
        if (!entering_) { entry_.clear(); entering_ = true; }
        rhsReady_ = true; // as above
        if (entry_.find_first_of("eE") != std::string::npos) entry_.clear(); // as above
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
            // repeating the operator with no entry: "5 + + *" -> 5+5, then multiplication
            if (!apply(acc_, pendingOp_, cur)) return;
        }
        // A different operator with no entry = just a swap ("5 + *" -> then multiplication).
        // entry_ empty: the display shows acc_ just as after "=".
        entry_.clear();
        pendingOp_ = op;
        entering_ = false;
        justEvaluated_ = false;
        // After a repeat acc_ already holds the result — the next "=" only
        // confirms it, it doesn't compute again (10+10). Without a repeat "=" may
        // compute with acc_ as the right operand ("5 + =" -> 10).
        rhsReady_ = !repeat;
    }

    void equals() {
        if (error_ || !pendingOp_) return;
        if (rhsReady_) {
            if (!apply(acc_, pendingOp_, currentEntry())) return;
        }
        pendingOp_ = 0;
        entering_ = false;
        entry_.clear(); // the display now shows acc_
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
            acc_ = -acc_; // empty entry -> the display shows acc_
        }
        justEvaluated_ = false;
    }

    // % and x^2 act ONLY on the current entry (the display) and leave
    // the accumulator and the pending operator alone - so "10 + 20 % " = 20% is
    // the base of the second operand, and "2 x^2 + 3 =" = 4 + 3 = 7.
    void percent() {
        if (error_) return;
        double v = currentEntry() / 100.0;
        if (!std::isfinite(v)) { error_ = true; return; }
        entry_ = format(v);
        entering_ = true;
        justEvaluated_ = false;
        rhsReady_ = true; // fresh operand after the recalculation
    }

    void square() {
        if (error_) return;
        double v = currentEntry() * currentEntry();
        if (!std::isfinite(v)) { error_ = true; return; }
        entry_ = format(v);
        entering_ = true;
        justEvaluated_ = false;
        rhsReady_ = true; // as above
    }

    // Text for the display (right-aligned; the view aligns it itself).
    std::string display() const {
        if (error_) return "Error";
        if (entry_.empty()) return format(acc_);
        std::string s = entry_;
        if (s.back() == '.') s.pop_back(); // "2." -> "2"
        if (s.empty()) return "0";         // just "." -> show "0"
        return s;
    }

private:
    double acc_ = 0.0;          // accumulator (first operand / result)
    std::string entry_;         // current entry (digits pushed by the user)
    char pendingOp_ = 0;        // pending operator: + - * /
    bool entering_ = false;     // true = pushing into entry_
    bool justEvaluated_ = false;// true = "=" was just pressed
    bool rhsReady_ = true;      // "=" may use acc_ as the right operand
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

    // Returns true = success; false = error (acc_ NOT changed).
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
