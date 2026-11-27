#include "core/Expression.h"

#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

namespace fw {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool isIdentStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool isIdentChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

class Parser {
public:
    Parser(const std::string& s, const ParamLookup& lookup, std::set<std::string>* names)
        : m_s(s), m_lookup(lookup), m_names(names) {}

    double parse()
    {
        skip();
        if (m_i >= m_s.size()) throw std::runtime_error("Leerer Ausdruck");
        const double v = expression();
        skip();
        if (m_i < m_s.size()) throw std::runtime_error("Unerwartetes Zeichen '" + m_s.substr(m_i, 1) + "'");
        return v;
    }

private:
    void skip()
    {
        while (m_i < m_s.size() && std::isspace(static_cast<unsigned char>(m_s[m_i]))) ++m_i;
    }
    bool eat(char c)
    {
        skip();
        if (m_i < m_s.size() && m_s[m_i] == c) { ++m_i; return true; }
        return false;
    }

    double expression()
    {
        double v = term();
        for (;;) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
    double term()
    {
        double v = power();
        for (;;) {
            if (eat('*')) v *= power();
            else if (eat('/')) {
                const double d = power();
                if (std::abs(d) < 1e-300) throw std::runtime_error("Division durch 0");
                v /= d;
            } else return v;
        }
    }
    double power()
    {
        const double base = unary();
        if (eat('^')) return std::pow(base, power());
        return base;
    }
    double unary()
    {
        if (eat('-')) return -unary();
        if (eat('+')) return unary();
        return primary();
    }

    std::string ident()
    {
        const size_t start = m_i;
        while (m_i < m_s.size() && isIdentChar(static_cast<unsigned char>(m_s[m_i]))) ++m_i;
        return m_s.substr(start, m_i - start);
    }

    // Optionale Einheit direkt nach einer Zahl oder Klammer
    double unit()
    {
        skip();
        const size_t save = m_i;
        if (m_s.compare(m_i, 2, "\xC2\xB0") == 0) { m_i += 2; return 1.0; } // °
        if (m_i < m_s.size() && isIdentStart(static_cast<unsigned char>(m_s[m_i]))) {
            const std::string u = ident();
            if (u == "mm" || u == "grd" || u == "deg" || u == "ul") return 1.0;
            if (u == "cm") return 10.0;
            if (u == "m") return 1000.0;
            if (u == "in") return 25.4;
            m_i = save;
        }
        return 1.0;
    }

    double primary()
    {
        skip();
        if (m_i >= m_s.size()) throw std::runtime_error("Ausdruck endet zu frueh");
        const unsigned char c = static_cast<unsigned char>(m_s[m_i]);
        if (c == '(') {
            ++m_i;
            const double v = expression();
            if (!eat(')')) throw std::runtime_error("Schliessende Klammer fehlt");
            return v * unit();
        }
        if (std::isdigit(c) || c == '.' || c == ',') {
            std::string num;
            while (m_i < m_s.size()) {
                const char d = m_s[m_i];
                if (std::isdigit(static_cast<unsigned char>(d))) num += d;
                else if ((d == '.' || d == ',') && num.find('.') == std::string::npos) num += '.';
                else if ((d == 'e' || d == 'E') && m_i + 1 < m_s.size() &&
                         (std::isdigit(static_cast<unsigned char>(m_s[m_i + 1])) || m_s[m_i + 1] == '-')) {
                    num += 'e';
                    if (m_s[m_i + 1] == '-') { num += '-'; ++m_i; }
                } else break;
                ++m_i;
            }
            if (num == ".") throw std::runtime_error("Ungueltige Zahl");
            return std::stod(num) * unit();
        }
        if (isIdentStart(c)) {
            const std::string name = ident();
            skip();
            if (m_i < m_s.size() && m_s[m_i] == '(') {
                ++m_i;
                std::vector<double> args;
                if (!eat(')')) {
                    args.push_back(expression());
                    while (eat(';')) args.push_back(expression());
                    if (!eat(')')) throw std::runtime_error("Schliessende Klammer fehlt");
                }
                return call(name, args);
            }
            if (name == "PI" || name == "pi") return kPi;
            if (m_names) m_names->insert(name);
            double v = 0;
            if (!m_lookup || !m_lookup(name, v)) throw std::runtime_error("Unbekannter Parameter '" + name + "'");
            return v;
        }
        throw std::runtime_error("Unerwartetes Zeichen '" + m_s.substr(m_i, 1) + "'");
    }

    static double call(const std::string& f, const std::vector<double>& a)
    {
        auto need = [&](size_t n) {
            if (a.size() != n) throw std::runtime_error("Funktion " + f + " erwartet " + std::to_string(n) + " Argument(e)");
        };
        const double deg = kPi / 180.0;
        if (f == "sin") { need(1); return std::sin(a[0] * deg); }
        if (f == "cos") { need(1); return std::cos(a[0] * deg); }
        if (f == "tan") { need(1); return std::tan(a[0] * deg); }
        if (f == "sqrt") {
            need(1);
            if (a[0] < 0) throw std::runtime_error("Wurzel aus negativer Zahl");
            return std::sqrt(a[0]);
        }
        if (f == "abs") { need(1); return std::abs(a[0]); }
        if (f == "round") { need(1); return std::round(a[0]); }
        if (f == "floor") { need(1); return std::floor(a[0]); }
        if (f == "ceil") { need(1); return std::ceil(a[0]); }
        if (f == "min" || f == "max") {
            if (a.empty()) throw std::runtime_error("Funktion " + f + " braucht Argumente");
            double v = a[0];
            for (double x : a) v = f == "min" ? std::min(v, x) : std::max(v, x);
            return v;
        }
        throw std::runtime_error("Unbekannte Funktion '" + f + "'");
    }

    const std::string& m_s;
    const ParamLookup& m_lookup;
    std::set<std::string>* m_names;
    size_t m_i = 0;
};

} // namespace

double evaluate(const std::string& expr, const ParamLookup& lookup)
{
    Parser p(expr, lookup, nullptr);
    const double v = p.parse();
    if (!std::isfinite(v)) throw std::runtime_error("Ergebnis ist keine endliche Zahl");
    return v;
}

std::vector<std::string> referencedNames(const std::string& expr)
{
    std::set<std::string> names;
    const ParamLookup any = [](const std::string&, double& v) { v = 1.0; return true; };
    try {
        Parser(expr, any, &names).parse();
    } catch (...) {
    }
    return {names.begin(), names.end()};
}

bool isPlainNumber(const std::string& expr)
{
    try {
        std::set<std::string> names;
        const std::string trimmed = expr;
        // Operatoren ausser einem fuehrenden Minus machen daraus eine Formel
        bool first = true;
        for (char c : trimmed) {
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            if ((c == '+' || c == '*' || c == '/' || c == '^' || c == '(' || c == ';') || (c == '-' && !first)) return false;
            first = false;
        }
        Parser(expr, ParamLookup{}, &names).parse();
        return names.empty();
    } catch (...) {
        return false;
    }
}

bool isValidParamName(const std::string& name)
{
    if (name.empty() || !isIdentStart(static_cast<unsigned char>(name[0]))) return false;
    for (unsigned char c : name)
        if (!isIdentChar(c)) return false;
    static const std::set<std::string> reserved = {"mm", "cm", "m", "in", "grd", "deg", "ul", "PI", "pi",
                                                   "sin", "cos", "tan", "sqrt", "abs", "round", "floor", "ceil", "min", "max"};
    return !reserved.count(name);
}

} // namespace fw
