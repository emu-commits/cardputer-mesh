// eval — a small, dependency-free expression + unit-conversion engine.
// Portable (core): the brains of the Calc app; reused unchanged on device.
//
// Supports (best-of qalc/bc/Soulver, scaled to the screen):
//   - arithmetic: + - * / % ^ (right-assoc power), unary +/-, parentheses
//   - constants: pi, e, ans (last result, supplied by caller)
//   - functions: sqrt abs sin cos tan asin acos atan log ln exp floor ceil round
//   - unit conversion: "<expr> <unit> in|to <unit>"  e.g. "10 km in mi",
//     "100 f to c", "1 gb in mb". Categories: length, mass, time, temp, data.
#pragma once
#include <string>

namespace calc {

struct Result {
    bool ok = false;
    double value = 0;       // numeric result (for chaining via `ans`)
    std::string display;    // pretty result, e.g. "42", "6.2137 mi"
    std::string error;      // set when !ok
};

// Evaluate one line. `ans` substitutes the identifier "ans".
Result evaluate(const std::string& input, double ans = 0);

// Format a double for display: integers without a decimal point, otherwise a
// trimmed general format.
std::string format(double v);

} // namespace calc
