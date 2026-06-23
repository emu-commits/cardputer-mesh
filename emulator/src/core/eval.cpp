#include "core/eval.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace calc {

std::string format(double v) {
    if (!std::isfinite(v)) return std::isnan(v) ? "nan" : (v < 0 ? "-inf" : "inf");
    if (std::fabs(v) < 1e15 && std::fabs(v - std::round(v)) < 1e-9) {
        char b[32]; std::snprintf(b, sizeof b, "%.0f", std::round(v)); return b;
    }
    char b[40]; std::snprintf(b, sizeof b, "%.10g", v); return b;
}

namespace {

// ---- recursive-descent expression parser ----------------------------------
struct Parser {
    const char* p;
    double ans;
    bool ok = true;
    std::string err;

    Parser(const char* src, double a) : p(src), ans(a) {}

    void skip() { while (*p == ' ' || *p == '\t') ++p; }
    void fail(const std::string& m) { if (ok) { ok = false; err = m; } }

    double parse() {
        double v = expr();
        skip();
        if (ok && *p != '\0') fail("unexpected '" + std::string(1, *p) + "'");
        return v;
    }

    double expr() { // + -
        double v = term();
        for (;;) {
            skip();
            char c = *p;
            if (c == '+' || c == '-') { ++p; double r = term(); v = (c == '+') ? v + r : v - r; }
            else break;
        }
        return v;
    }
    double term() { // * / %
        double v = unary();
        for (;;) {
            skip();
            char c = *p;
            if (c == '*' || c == '/' || c == '%') {
                ++p; double r = unary();
                if (c == '*') v *= r;
                else if (c == '/') { if (r == 0) { fail("divide by zero"); return 0; } v /= r; }
                else { if (r == 0) { fail("mod by zero"); return 0; } v = std::fmod(v, r); }
            } else break;
        }
        return v;
    }
    double unary() { // unary +/- binds looser than ^, so -2^2 == -(2^2)
        skip();
        if (*p == '+') { ++p; return unary(); }
        if (*p == '-') { ++p; return -unary(); }
        return power();
    }
    double power() { // ^ (right assoc); exponent may carry its own sign
        double base = primary();
        skip();
        if (*p == '^') { ++p; double e = unary(); return std::pow(base, e); }
        return base;
    }
    double primary() {
        skip();
        if (*p == '(') {
            ++p; double v = expr(); skip();
            if (*p == ')') ++p; else fail("expected ')'");
            return v;
        }
        if (std::isdigit((unsigned char)*p) || *p == '.') {
            char* end = nullptr; double v = std::strtod(p, &end);
            if (end == p) { fail("bad number"); return 0; }
            p = end; return v;
        }
        if (std::isalpha((unsigned char)*p)) {
            std::string id;
            while (std::isalpha((unsigned char)*p)) id += (char)std::tolower(*p++);
            skip();
            if (*p == '(') { ++p; double a = expr(); skip(); if (*p == ')') ++p; else fail("expected ')'"); return apply(id, a); }
            if (id == "pi") return M_PI;
            if (id == "e") return M_E;
            if (id == "ans") return ans;
            fail("unknown name '" + id + "'"); return 0;
        }
        fail("unexpected '" + std::string(1, *p ? *p : '?') + "'");
        return 0;
    }
    double apply(const std::string& f, double a) {
        if (f == "sqrt") return std::sqrt(a);
        if (f == "abs")  return std::fabs(a);
        if (f == "sin")  return std::sin(a);
        if (f == "cos")  return std::cos(a);
        if (f == "tan")  return std::tan(a);
        if (f == "asin") return std::asin(a);
        if (f == "acos") return std::acos(a);
        if (f == "atan") return std::atan(a);
        if (f == "log")  return std::log10(a);
        if (f == "ln")   return std::log(a);
        if (f == "exp")  return std::exp(a);
        if (f == "floor")return std::floor(a);
        if (f == "ceil") return std::ceil(a);
        if (f == "round")return std::round(a);
        fail("unknown function '" + f + "'"); return 0;
    }
};

// ---- units -----------------------------------------------------------------
struct Unit { const char* cat; double factor; }; // value_in_base = value * factor

const std::unordered_map<std::string, Unit>& unit_table() {
    static const std::unordered_map<std::string, Unit> t = {
        // length (base: meter)
        {"m",{"len",1}},{"km",{"len",1000}},{"cm",{"len",0.01}},{"mm",{"len",0.001}},
        {"mi",{"len",1609.344}},{"ft",{"len",0.3048}},{"in",{"len",0.0254}},
        {"yd",{"len",0.9144}},{"nmi",{"len",1852}},
        // mass (base: gram)
        {"g",{"mass",1}},{"kg",{"mass",1000}},{"mg",{"mass",0.001}},
        {"lb",{"mass",453.59237}},{"oz",{"mass",28.349523}},{"t",{"mass",1e6}},{"st",{"mass",6350.29}},
        // time (base: second)
        {"s",{"time",1}},{"ms",{"time",0.001}},{"min",{"time",60}},{"h",{"time",3600}},
        {"hr",{"time",3600}},{"d",{"time",86400}},{"wk",{"time",604800}},
        // data (base: byte)
        {"b",{"data",1}},{"kb",{"data",1024}},{"mb",{"data",1048576}},
        {"gb",{"data",1073741824}},{"tb",{"data",1099511627776.0}},
    };
    return t;
}

double to_celsius(double v, const std::string& u) {
    if (u == "c") return v;
    if (u == "f") return (v - 32) * 5.0 / 9.0;
    if (u == "k") return v - 273.15;
    return NAN;
}
double from_celsius(double c, const std::string& u) {
    if (u == "c") return c;
    if (u == "f") return c * 9.0 / 5.0 + 32;
    if (u == "k") return c + 273.15;
    return NAN;
}
bool is_temp(const std::string& u) { return u == "c" || u == "f" || u == "k"; }

std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t"); return s.substr(a, b - a + 1);
}

Result convert(const std::string& leftRaw, const std::string& toUnitRaw, double ans) {
    Result r;
    std::string toUnit = lower(trim(toUnitRaw));
    std::string left = trim(leftRaw);
    // split trailing unit token off `left`: "<expr> <unit>"
    size_t sp = left.find_last_of(" \t");
    if (sp == std::string::npos) { r.error = "need '<value> <unit>'"; return r; }
    std::string exprPart = trim(left.substr(0, sp));
    std::string fromUnit = lower(trim(left.substr(sp + 1)));

    Parser ps{exprPart.c_str(), ans};
    double val = ps.parse();
    if (!ps.ok) { r.error = ps.err; return r; }

    if (is_temp(fromUnit) || is_temp(toUnit)) {
        if (!is_temp(fromUnit) || !is_temp(toUnit)) { r.error = "temp<->non-temp"; return r; }
        double c = to_celsius(val, fromUnit);
        double out = from_celsius(c, toUnit);
        r.ok = true; r.value = out; r.display = format(out) + " " + toUnit; return r;
    }
    const auto& T = unit_table();
    auto fi = T.find(fromUnit), ti = T.find(toUnit);
    if (fi == T.end()) { r.error = "unknown unit '" + fromUnit + "'"; return r; }
    if (ti == T.end()) { r.error = "unknown unit '" + toUnit + "'"; return r; }
    if (std::strcmp(fi->second.cat, ti->second.cat) != 0) { r.error = "incompatible units"; return r; }
    double out = val * fi->second.factor / ti->second.factor;
    r.ok = true; r.value = out; r.display = format(out) + " " + toUnit; return r;
}

} // namespace

Result evaluate(const std::string& input, double ans) {
    Result r;
    std::string s = trim(input);
    if (s.empty()) { r.error = ""; return r; }

    // unit conversion?  "<left> in <unit>" / "<left> to <unit>"
    std::string ls = lower(s);
    size_t pos = std::string::npos; size_t kwlen = 0;
    for (const char* kw : {" in ", " to "}) {
        size_t f = ls.rfind(kw);
        if (f != std::string::npos && (pos == std::string::npos || f > pos)) { pos = f; kwlen = std::strlen(kw); }
    }
    if (pos != std::string::npos) {
        std::string right = trim(s.substr(pos + kwlen));
        // only treat as conversion if the right side is a single bare unit token
        if (!right.empty() && right.find(' ') == std::string::npos &&
            std::isalpha((unsigned char)right[0]))
            return convert(s.substr(0, pos), right, ans);
    }

    Parser ps{s.c_str(), ans};
    double v = ps.parse();
    if (!ps.ok) { r.error = ps.err; return r; }
    r.ok = true; r.value = v; r.display = format(v);
    return r;
}

} // namespace calc
