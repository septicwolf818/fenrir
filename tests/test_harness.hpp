#pragma once
#include <stdexcept>
#include <string>

namespace fenrir::test {

// A failed check throws; the runner (tests/main.cpp) catches it, reports the
// suite as failed and continues with the next one.
class Failure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Checks a condition; throws Failure with the message when it does not hold.
inline void check(bool cond, const std::string& message) {
    if (!cond) throw Failure(message);
}

} // namespace fenrir::test
