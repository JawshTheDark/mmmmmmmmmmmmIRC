// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The standard MSL identifiers and commands. Behaviour follows mIRC's published
// help; each entry is covered by test_msl. This is a first tranche (text, token,
// number, time, control and send commands); the long tail is added against the
// parity matrix.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <regex>
#include <string>

#include "core/msl/Interpreter.h"

namespace lurker::msl {

namespace {

std::string arg(const std::vector<std::string>& a, std::size_t i) { return i < a.size() ? a[i] : std::string(); }
int argInt(const std::vector<std::string>& a, std::size_t i, int def = 0) {
    try {
        return i < a.size() && !a[i].empty() ? std::stoi(a[i]) : def;
    } catch (...) {
        return def;
    }
}

std::string fmtNum(double v) {
    if (std::floor(v) == v && std::abs(v) < 1e15) return std::to_string(static_cast<long long>(v));
    std::string s = std::to_string(v);
    while (s.find('.') != std::string::npos && (s.back() == '0')) s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// --- $calc arithmetic: recursive descent over + - * / % ^ and parens --------
struct Calc {
    std::string_view s;
    std::size_t i = 0;
    bool ok = true;
    void skip() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    double expr() {
        double v = term();
        for (;;) {
            skip();
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
                char op = s[i++];
                double r = term();
                v = op == '+' ? v + r : v - r;
            } else break;
        }
        return v;
    }
    double term() {
        double v = power();
        for (;;) {
            skip();
            if (i < s.size() && (s[i] == '*' || s[i] == '/' || s[i] == '%')) {
                char op = s[i++];
                double r = power();
                if (op == '*') v *= r;
                else if (op == '/') v = r != 0 ? v / r : 0;
                else v = r != 0 ? std::fmod(v, r) : 0;
            } else break;
        }
        return v;
    }
    double power() {
        double v = factor();
        skip();
        if (i < s.size() && s[i] == '^') {
            ++i;
            v = std::pow(v, power());
        }
        return v;
    }
    double factor() {
        skip();
        if (i < s.size() && s[i] == '(') {
            ++i;
            double v = expr();
            skip();
            if (i < s.size() && s[i] == ')') ++i;
            return v;
        }
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) {
            char op = s[i++];
            double v = factor();
            return op == '-' ? -v : v;
        }
        std::size_t start = i;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
        if (i == start) {
            ok = false;
            return 0;
        }
        try {
            return std::stod(std::string(s.substr(start, i - start)));
        } catch (...) {
            ok = false;
            return 0;
        }
    }
};

std::mt19937& rng() {
    static std::mt19937 gen{std::random_device{}()};
    return gen;
}

// --- INI helpers (mIRC .ini files: [section] then item=value, case-insensitive)
std::string iniStrip(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}
bool ciEq(const std::string& a, const std::string& b) { return toLowerAscii(a) == toLowerAscii(b); }

std::string readIniValue(const std::string& content, const std::string& section, const std::string& item) {
    bool inSec = false;
    for (const auto& raw : splitTokens(content, '\n')) {
        std::string line = iniStrip(raw);
        if (line.empty() || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            inSec = ciEq(line.substr(1, line.size() - 2), section);
            continue;
        }
        if (!inSec) continue;
        const std::size_t eq = line.find('=');
        if (eq != std::string::npos && ciEq(iniStrip(line.substr(0, eq)), item)) return iniStrip(line.substr(eq + 1));
    }
    return {};
}
std::string iniSectionByIndex(const std::string& content, int n) {
    int seen = 0;
    for (const auto& raw : splitTokens(content, '\n')) {
        std::string line = iniStrip(raw);
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']' && ++seen == n)
            return line.substr(1, line.size() - 2);
    }
    return {};
}
std::string iniItemByIndex(const std::string& content, const std::string& section, int n) {
    bool inSec = false;
    int seen = 0;
    for (const auto& raw : splitTokens(content, '\n')) {
        std::string line = iniStrip(raw);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            inSec = ciEq(line.substr(1, line.size() - 2), section);
            continue;
        }
        if (inSec) {
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos && ++seen == n) return iniStrip(line.substr(0, eq));
        }
    }
    return {};
}
// Returns `content` with [section] item=value set (section/item created as needed).
std::string writeIniValue(const std::string& content, const std::string& section, const std::string& item,
                          const std::string& value) {
    std::vector<std::string> lines = splitTokens(content, '\n');
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    int secStart = -1, secEnd = static_cast<int>(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string line = iniStrip(lines[i]);
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            if (secStart < 0 && ciEq(line.substr(1, line.size() - 2), section)) {
                secStart = static_cast<int>(i);
            } else if (secStart >= 0) {
                secEnd = static_cast<int>(i);
                break;
            }
        }
    }
    const std::string entry = item + "=" + value;
    if (secStart < 0) {
        if (!lines.empty()) lines.push_back("");
        lines.push_back("[" + section + "]");
        lines.push_back(entry);
    } else {
        bool replaced = false;
        for (int i = secStart + 1; i < secEnd; ++i) {
            std::string line = iniStrip(lines[i]);
            const std::size_t eq = line.find('=');
            if (eq != std::string::npos && ciEq(iniStrip(line.substr(0, eq)), item)) {
                lines[i] = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced) lines.insert(lines.begin() + secEnd, entry);
    }
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

// Parse an mIRC-style regex argument: optional /pattern/flags, returning the
// pattern and the std::regex flags plus whether the g (global) flag was set.
std::regex compileRegex(const std::string& raw, bool& global) {
    std::string pat = raw;
    auto flags = std::regex::ECMAScript;
    global = false;
    if (pat.size() >= 2 && pat.front() == '/') {
        const std::size_t close = pat.rfind('/');
        if (close > 0) {
            const std::string mods = pat.substr(close + 1);
            pat = pat.substr(1, close - 1);
            for (char c : mods) {
                if (c == 'i' || c == 'I') flags |= std::regex::icase;
                else if (c == 'm' || c == 'M') flags |= std::regex::multiline;
                else if (c == 'g' || c == 'G') global = true;
            }
        }
    }
    return std::regex(pat, flags);
}

}  // namespace

void registerBuiltins(Interpreter& in) {
    using S = Scope;
    using A = std::vector<std::string>;

    // --- constants ---
    in.registerIdent("null", [](Interpreter&, S&, const A&, const std::string&) { return std::string(); });
    in.registerIdent("true", [](Interpreter&, S&, const A&, const std::string&) { return std::string("$true"); });
    in.registerIdent("false", [](Interpreter&, S&, const A&, const std::string&) { return std::string("$false"); });
    in.registerIdent("cr", [](Interpreter&, S&, const A&, const std::string&) { return std::string("\r"); });
    in.registerIdent("lf", [](Interpreter&, S&, const A&, const std::string&) { return std::string("\n"); });
    in.registerIdent("crlf", [](Interpreter&, S&, const A&, const std::string&) { return std::string("\r\n"); });
    in.registerIdent("tab", [](Interpreter&, S&, const A&, const std::string&) { return std::string("\t"); });

    // --- context ---
    in.registerIdent("me", [](Interpreter& i, S&, const A&, const std::string&) {
        return i.host().me(i.host().activeTarget().networkId);
    });
    in.registerIdent("chan", [](Interpreter& i, S& s, const A&, const std::string&) {
        return s.chan.empty() ? i.host().activeTarget().window : s.chan;
    });
    in.registerIdent("active", [](Interpreter& i, S&, const A&, const std::string&) {
        return i.host().activeTarget().window;
    });
    in.registerIdent("event", [](Interpreter&, S& s, const A&, const std::string&) { return s.event; });
    in.registerIdent("prop", [](Interpreter&, S& s, const A&, const std::string&) { return s.prop; });
    // Dialog event context + control access.
    in.registerIdent("dname", [](Interpreter&, S& s, const A&, const std::string&) {
        return s.params.empty() ? s.target : s.params[0];
    });
    in.registerIdent("devent", [](Interpreter&, S& s, const A&, const std::string&) {
        return s.params.size() >= 2 ? s.params[1] : std::string();
    });
    in.registerIdent("did", [](Interpreter& in2, S& s, const A& a, const std::string& prop) {
        if (a.empty()) return s.params.size() >= 3 ? s.params[2] : std::string();  // triggering id
        const std::string name = a[0];
        const int id = a.size() >= 2 ? argInt(a, 1) : 0;
        return in2.host().didQuery(name, id, prop);
    });
    in.registerIdent("dialog", [](Interpreter& in2, S&, const A& a, const std::string& prop) {
        if (a.empty()) return std::string();
        return in2.host().didQuery(a[0], 0, prop.empty() ? std::string("isopen") : prop);
    });
    // Event context: set while an on-handler runs.
    in.registerIdent("nick", [](Interpreter&, S& s, const A&, const std::string&) { return s.nick; });
    in.registerIdent("target", [](Interpreter& i, S& s, const A&, const std::string&) {
        return s.target.empty() ? i.host().activeTarget().window : s.target;
    });
    in.registerIdent("address", [](Interpreter&, S& s, const A&, const std::string&) { return s.address; });
    in.registerIdent("site", [](Interpreter&, S& s, const A&, const std::string&) {
        const std::size_t at = s.address.find('@');
        return at == std::string::npos ? std::string() : s.address.substr(at + 1);
    });

    // --- text/number ---
    in.registerIdent("len", [](Interpreter&, S&, const A& a, const std::string&) {
        return std::to_string(arg(a, 0).size());
    });
    in.registerIdent("upper", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        return s;
    });
    in.registerIdent("lower", [](Interpreter&, S&, const A& a, const std::string&) { return toLowerAscii(arg(a, 0)); });
    in.registerIdent("left", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        int n = argInt(a, 1);
        if (n < 0) return s.substr(0, s.size() > static_cast<std::size_t>(-n) ? s.size() + n : 0);
        return s.substr(0, std::min<std::size_t>(n, s.size()));
    });
    in.registerIdent("right", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        int n = argInt(a, 1);
        if (n < 0) {
            std::size_t off = std::min<std::size_t>(-n, s.size());
            return s.substr(off);
        }
        return s.substr(s.size() - std::min<std::size_t>(n, s.size()));
    });
    in.registerIdent("mid", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        int start = argInt(a, 1, 1);
        if (start < 1) start = 1;
        if (static_cast<std::size_t>(start) > s.size()) return std::string();
        std::size_t from = start - 1;
        if (a.size() >= 3) return s.substr(from, std::max(0, argInt(a, 2)));
        return s.substr(from);
    });
    in.registerIdent("str", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string out;
        int n = argInt(a, 1);
        for (int k = 0; k < n; ++k) out += arg(a, 0);
        return out;
    });
    in.registerIdent("chr", [](Interpreter&, S&, const A& a, const std::string&) {
        return std::string(1, static_cast<char>(argInt(a, 0)));
    });
    in.registerIdent("asc", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        return std::to_string(s.empty() ? 0 : static_cast<unsigned char>(s[0]));
    });
    in.registerIdent("replace", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        for (std::size_t k = 1; k + 1 < a.size(); k += 2) {
            const std::string& from = a[k];
            const std::string& to = a[k + 1];
            if (from.empty()) continue;
            std::size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            }
        }
        return s;
    });
    in.registerIdent("remove", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        for (std::size_t k = 1; k < a.size(); ++k) {
            const std::string& from = a[k];
            if (from.empty()) continue;
            std::size_t pos = 0;
            while ((pos = s.find(from, pos)) != std::string::npos) s.erase(pos, from.size());
        }
        return s;
    });
    in.registerIdent("pos", [](Interpreter&, S&, const A& a, const std::string&) {
        const std::string& s = arg(a, 0);
        const std::string& sub = arg(a, 1);
        std::size_t p = s.find(sub);
        return std::to_string(p == std::string::npos ? 0 : p + 1);
    });
    in.registerIdent("count", [](Interpreter&, S&, const A& a, const std::string&) {
        const std::string& s = arg(a, 0);
        const std::string& sub = arg(a, 1);
        if (sub.empty()) return std::string("0");
        int n = 0;
        for (std::size_t p = s.find(sub); p != std::string::npos; p = s.find(sub, p + sub.size())) ++n;
        return std::to_string(n);
    });

    // --- tokens ($gettok et al; separator is an ASCII code) ---
    auto sepChar = [](const A& a, std::size_t idx) -> char {
        return static_cast<char>(argInt(a, idx, 32));
    };
    in.registerIdent("gettok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        auto toks = splitTokens(arg(a, 0), sepChar(a, 2));
        int n = argInt(a, 1);
        if (n < 0) n = static_cast<int>(toks.size()) + n + 1;
        return (n >= 1 && n <= static_cast<int>(toks.size())) ? toks[n - 1] : std::string();
    });
    in.registerIdent("numtok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        return std::to_string(arg(a, 0).empty() ? 0 : splitTokens(arg(a, 0), sepChar(a, 1)).size());
    });
    in.registerIdent("addtok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        char sep = sepChar(a, 2);
        auto toks = splitTokens(arg(a, 0), sep);
        const std::string& add = arg(a, 1);
        for (const auto& t : toks) {
            if (t == add) return arg(a, 0);
        }
        std::string out = arg(a, 0);
        if (!out.empty()) out += sep;
        out += add;
        return out;
    });
    in.registerIdent("istok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        auto toks = splitTokens(arg(a, 0), sepChar(a, 2));
        const std::string& find = arg(a, 1);
        for (const auto& t : toks) {
            if (toLowerAscii(t) == toLowerAscii(find)) return std::string("$true");
        }
        return std::string("$false");
    });
    in.registerIdent("findtok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        auto toks = splitTokens(arg(a, 0), sepChar(a, 3));
        const std::string find = arg(a, 1);
        int which = argInt(a, 2, 1);
        if (which == 0) which = 1;
        int seen = 0;
        for (std::size_t k = 0; k < toks.size(); ++k) {
            if (toLowerAscii(toks[k]) == toLowerAscii(find) && ++seen == which) return std::to_string(k + 1);
        }
        return std::string("0");
    });
    in.registerIdent("remtok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        char sep = sepChar(a, 3);
        auto toks = splitTokens(arg(a, 0), sep);
        const std::string rem = arg(a, 1);
        std::string out;
        bool removed = false;
        for (const auto& t : toks) {
            if (!removed && toLowerAscii(t) == toLowerAscii(rem)) {
                removed = true;
                continue;
            }
            if (!out.empty()) out += sep;
            out += t;
        }
        return out;
    });
    in.registerIdent("puttok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        char sep = sepChar(a, 3);
        auto toks = splitTokens(arg(a, 0), sep);
        int pos = argInt(a, 2);
        if (pos >= 1 && pos <= static_cast<int>(toks.size())) toks[pos - 1] = arg(a, 1);
        std::string out;
        for (std::size_t k = 0; k < toks.size(); ++k) out += (k ? std::string(1, sep) : std::string()) + toks[k];
        return out;
    });
    in.registerIdent("instok", [sepChar](Interpreter&, S&, const A& a, const std::string&) {
        char sep = sepChar(a, 3);
        auto toks = splitTokens(arg(a, 0), sep);
        int pos = argInt(a, 2, 1);
        if (pos < 1) pos = 1;
        toks.insert(toks.begin() + std::min<std::size_t>(pos - 1, toks.size()), arg(a, 1));
        std::string out;
        for (std::size_t k = 0; k < toks.size(); ++k) out += (k ? std::string(1, sep) : std::string()) + toks[k];
        return out;
    });

    // $qt / $noqt quoting.
    in.registerIdent("qt", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        if (s.find(' ') != std::string::npos && !(s.size() >= 2 && s.front() == '"' && s.back() == '"'))
            return "\"" + s + "\"";
        return s;
    });
    in.registerIdent("noqt", [](Interpreter&, S&, const A& a, const std::string&) {
        std::string s = arg(a, 0);
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
        return s;
    });
    in.registerIdent("isalias", [](Interpreter& i, S&, const A& a, const std::string&) {
        return i.aliasDefined(arg(a, 0)) ? std::string("$true") : std::string("$false");
    });

    // Regex: $regex([name],text,pattern) → match count; captures go to $regml.
    in.registerIdent("regex", [](Interpreter& in2, S&, const A& a, const std::string&) {
        std::string name = "default", text, pat;
        if (a.size() >= 3) {
            name = a[0];
            text = a[1];
            pat = a[2];
        } else if (a.size() == 2) {
            text = a[0];
            pat = a[1];
        } else {
            return std::string("0");
        }
        std::vector<std::string> groups;
        int matches = 0;
        try {
            bool global = false;
            std::regex re = compileRegex(pat, global);
            auto begin = std::sregex_iterator(text.begin(), text.end(), re);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                ++matches;
                const std::smatch& m = *it;
                for (std::size_t g = 1; g < m.size(); ++g) groups.push_back(m[g].str());
                if (!global) break;
            }
        } catch (...) {
            in2.regexStore(name, {});
            return std::string("0");
        }
        in2.regexStore(name, std::move(groups));
        return std::to_string(matches);
    });
    in.registerIdent("regml", [](Interpreter& in2, S&, const A& a, const std::string&) {
        if (a.size() >= 2) return in2.regmlGet(a[0], argInt(a, 1));
        return in2.regmlGet("default", argInt(a, 0));
    });
    in.registerIdent("regsub", [](Interpreter&, S&, const A& a, const std::string&) {
        // $regsub(text, pattern, subst) → replaced text (ECMAScript $1 refs).
        if (a.size() < 3) return arg(a, 0);
        try {
            bool global = false;
            std::regex re = compileRegex(arg(a, 1), global);
            return std::regex_replace(arg(a, 0), re, arg(a, 2),
                                      global ? std::regex_constants::format_default
                                             : std::regex_constants::format_first_only);
        } catch (...) {
            return arg(a, 0);
        }
    });

    // Hash tables.
    in.registerIdent("hget", [](Interpreter& i, S&, const A& a, const std::string& prop) {
        const std::string table = arg(a, 0);
        if (a.size() < 2) return i.hashExists(table) ? std::string("1") : std::string("0");
        // $hget(table, N).item / .data  — numeric item index when prop is item/data
        if ((prop == "item" || prop == "data")) {
            int n = argInt(a, 1);
            return prop == "item" ? i.hashItemByIndex(table, n) : i.hashDataByIndex(table, n);
        }
        return i.hashGet(table, arg(a, 1));
    });

    // --- math ---
    in.registerIdent("calc", [](Interpreter&, S&, const A& a, const std::string&) {
        Calc c{arg(a, 0)};
        double v = c.expr();
        return c.ok ? fmtNum(v) : std::string();
    });
    in.registerIdent("abs", [](Interpreter&, S&, const A& a, const std::string&) {
        try {
            return fmtNum(std::abs(std::stod(arg(a, 0))));
        } catch (...) {
            return std::string();
        }
    });
    in.registerIdent("int", [](Interpreter&, S&, const A& a, const std::string&) {
        try {
            return std::to_string(static_cast<long long>(std::stod(arg(a, 0))));
        } catch (...) {
            return std::string();
        }
    });
    in.registerIdent("round", [](Interpreter&, S&, const A& a, const std::string&) {
        try {
            double v = std::stod(arg(a, 0));
            int d = argInt(a, 1);
            double f = std::pow(10, d);
            return fmtNum(std::round(v * f) / f);
        } catch (...) {
            return std::string();
        }
    });
    auto randFn = [](Interpreter&, S&, const A& a, const std::string&) {
        int lo = argInt(a, 0), hi = argInt(a, 1);
        if (lo > hi) std::swap(lo, hi);
        std::uniform_int_distribution<int> dist(lo, hi);
        return std::to_string(dist(rng()));
    };
    in.registerIdent("rand", randFn);
    in.registerIdent("r", randFn);
    in.registerIdent("iif", [](Interpreter& interp, S& s, const A& a, const std::string&) {
        return interp.evalCondition(arg(a, 0), s) ? arg(a, 1) : arg(a, 2);
    });

    // --- time ---
    in.registerIdent("ctime", [](Interpreter& i, S&, const A&, const std::string&) {
        return std::to_string(i.host().unixTime());
    });

    // --- files and INI ---
    in.registerIdent("exists", [](Interpreter& i, S&, const A& a, const std::string&) {
        return (i.host().fileExists(arg(a, 0)) || i.host().dirExists(arg(a, 0))) ? std::string("$true")
                                                                                 : std::string("$false");
    });
    in.registerIdent("isfile", [](Interpreter& i, S&, const A& a, const std::string&) {
        return i.host().fileExists(arg(a, 0)) ? std::string("$true") : std::string("$false");
    });
    in.registerIdent("isdir", [](Interpreter& i, S&, const A& a, const std::string&) {
        return i.host().dirExists(arg(a, 0)) ? std::string("$true") : std::string("$false");
    });
    in.registerIdent("lines", [](Interpreter& i, S&, const A& a, const std::string&) {
        const std::string c = i.host().fileRead(arg(a, 0));
        if (c.empty()) return std::string("0");
        return std::to_string(static_cast<long long>(std::count(c.begin(), c.end(), '\n')) + (c.back() == '\n' ? 0 : 1));
    });
    in.registerIdent("read", [](Interpreter& i, S&, const A& a, const std::string&) {
        const auto lines = splitTokens(i.host().fileRead(arg(a, 0)), '\n');
        if (lines.empty()) return std::string();
        int n = a.size() >= 2 ? argInt(a, 1) : 1;
        if (n < 1 || n > static_cast<int>(lines.size())) return std::string();
        std::string line = lines[n - 1];
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    });
    in.registerIdent("readini", [](Interpreter& i, S&, const A& a, const std::string&) {
        return readIniValue(i.host().fileRead(arg(a, 0)), arg(a, 1), arg(a, 2));
    });
    in.registerIdent("ini", [](Interpreter& i, S&, const A& a, const std::string&) {
        // $ini(file,N) -> Nth section; $ini(file,section,N) -> Nth item in section.
        const std::string content = i.host().fileRead(arg(a, 0));
        if (a.size() == 2) return iniSectionByIndex(content, argInt(a, 1));
        if (a.size() >= 3) return iniItemByIndex(content, arg(a, 1), argInt(a, 2));
        return std::string();
    });
    in.registerIdent("file", [](Interpreter& i, S&, const A& a, const std::string& prop) {
        if (prop == "size") {
            long long sz = i.host().fileSize(arg(a, 0));
            return sz < 0 ? std::string("0") : std::to_string(sz);
        }
        return arg(a, 0);
    });
    in.registerIdent("findfile", [](Interpreter& i, S&, const A& a, const std::string&) {
        auto files = i.host().listDir(arg(a, 0), arg(a, 1).empty() ? std::string("*") : arg(a, 1));
        int n = argInt(a, 2, 1);
        return (n >= 1 && n <= static_cast<int>(files.size())) ? files[n - 1] : std::string();
    });

    // === commands ===
    // Commands receive RAW args and evaluate what they need (mIRC semantics):
    // message/echo text is evaluated; a target variable NAME is kept literal.
    auto echoCmd = [](Interpreter& i, S& sc, const std::string& rawArgs) {
        std::string rest = i.evaluate(rawArgs, sc);
        // Strip a leading -switches token and an optional target.
        ScriptTarget where = i.host().activeTarget();
        auto firstTok = [](const std::string& s) {
            std::size_t sp = s.find(' ');
            return sp == std::string::npos ? s : s.substr(0, sp);
        };
        std::string tok = firstTok(rest);
        if (!tok.empty() && tok[0] == '-') {
            rest = rest.size() > tok.size() ? rest.substr(tok.size() + 1) : std::string();
            tok = firstTok(rest);
        }
        if (!tok.empty() && (tok[0] == '#' || tok[0] == '@')) {
            where.window = tok;
            rest = rest.size() > tok.size() ? rest.substr(tok.size() + 1) : std::string();
        }
        i.host().echo(where, rest);
    };
    in.registerCommand("echo", echoCmd);

    in.registerCommand("say", [](Interpreter& i, S& sc, const std::string& a) {
        ScriptTarget t = i.host().activeTarget();
        i.host().sendMessage(t.networkId, t.window, i.evaluate(a, sc));
    });
    in.registerCommand("msg", [](Interpreter& i, S& sc, const std::string& a) {
        std::string args = i.evaluate(a, sc);
        std::size_t sp = args.find(' ');
        if (sp == std::string::npos) return;
        i.host().sendMessage(i.host().activeTarget().networkId, args.substr(0, sp), args.substr(sp + 1));
    });
    in.registerCommand("me", [](Interpreter& i, S& sc, const std::string& a) {
        ScriptTarget t = i.host().activeTarget();
        i.host().sendAction(t.networkId, t.window, i.evaluate(a, sc));
    });
    in.registerCommand("describe", [](Interpreter& i, S& sc, const std::string& a) {
        std::string args = i.evaluate(a, sc);
        std::size_t sp = args.find(' ');
        if (sp == std::string::npos) return;
        i.host().sendAction(i.host().activeTarget().networkId, args.substr(0, sp), args.substr(sp + 1));
    });
    in.registerCommand("raw", [](Interpreter& i, S& sc, const std::string& a) {
        i.host().sendRaw(i.host().activeTarget().networkId, i.evaluate(a, sc));
    });
    in.registerCommand("quote", [](Interpreter& i, S& sc, const std::string& a) {
        i.host().sendRaw(i.host().activeTarget().networkId, i.evaluate(a, sc));
    });

    // variables
    auto parseSetName = [](const std::string& args, std::string& name, std::string& value, bool& hasValue) {
        std::string s = args;
        // skip leading -switches
        if (!s.empty() && s[0] == '-') {
            std::size_t sp = s.find(' ');
            s = sp == std::string::npos ? std::string() : s.substr(sp + 1);
        }
        std::size_t sp = s.find(' ');
        std::string first = sp == std::string::npos ? s : s.substr(0, sp);
        if (!first.empty() && first[0] == '%') first = first.substr(1);
        name = first;
        if (sp == std::string::npos) {
            hasValue = false;
            return;
        }
        value = s.substr(sp + 1);
        // /var %x = value : drop a leading "= "
        std::string vtrim = value;
        if (vtrim.rfind("= ", 0) == 0) value = vtrim.substr(2);
        else if (vtrim == "=") value.clear();
        hasValue = true;
    };
    // /set and /var keep the %name literal but evaluate the value.
    in.registerCommand("set", [parseSetName](Interpreter& i, S& s, const std::string& args) {
        std::string name, value;
        bool has = false;
        parseSetName(args, name, value, has);
        if (!name.empty()) i.setVariable(name, i.evaluate(value, s), false, s);
    });
    in.registerCommand("var", [parseSetName](Interpreter& i, S& s, const std::string& args) {
        std::string name, value;
        bool has = false;
        parseSetName(args, name, value, has);
        if (!name.empty()) i.setVariable(name, i.evaluate(value, s), true, s);
    });
    auto incdec = [](bool inc) {
        return [inc](Interpreter& i, S& s, const std::string& args) {
            std::string a = args;
            if (!a.empty() && a[0] == '-') {
                std::size_t sp = a.find(' ');
                a = sp == std::string::npos ? std::string() : a.substr(sp + 1);
            }
            std::size_t sp = a.find(' ');
            std::string name = sp == std::string::npos ? a : a.substr(0, sp);
            if (!name.empty() && name[0] == '%') name = name.substr(1);
            double by = 1;
            if (sp != std::string::npos) {
                try {
                    by = std::stod(i.evaluate(a.substr(sp + 1), s));
                } catch (...) {
                }
            }
            double cur = 0;
            try {
                cur = std::stod(i.getVariable(name, s));
            } catch (...) {
            }
            const bool local = s.locals.count(name) != 0;
            i.setVariable(name, fmtNum(inc ? cur + by : cur - by), local, s);
        };
    };
    in.registerCommand("inc", incdec(true));
    in.registerCommand("dec", incdec(false));
    in.registerCommand("unset", [](Interpreter& i, S& s, const std::string& args) {
        for (const auto& t : splitTokens(args, ' ')) {
            std::string name = t;
            if (!name.empty() && name[0] == '%') name = name.substr(1);
            if (!name.empty()) i.unsetVariable(name, s);
        }
    });

    // control
    in.registerCommand("return", [](Interpreter& i, S& s, const std::string& args) {
        s.returnValue = i.evaluate(args, s);
        s.returned = true;
    });
    in.registerCommand("halt", [](Interpreter&, S& s, const std::string&) { s.halted = true; });
    in.registerCommand("haltdef", [](Interpreter&, S& s, const std::string&) { s.halted = true; });
    // Hash table commands.
    in.registerCommand("hmake", [](Interpreter& i, S& s, const std::string& a) {
        std::string args = i.evaluate(a, s);
        std::string name = args.substr(0, args.find(' '));
        if (!name.empty()) i.hashMake(name);
    });
    in.registerCommand("hfree", [](Interpreter& i, S& s, const std::string& a) {
        i.hashFree(i.evaluate(a, s).substr(0, i.evaluate(a, s).find(' ')));
    });
    in.registerCommand("hadd", [](Interpreter& i, S& s, const std::string& a) {
        // /hadd [-switches] <table> <item> <data>
        std::string args = i.evaluate(a, s);
        if (!args.empty() && args[0] == '-') args = args.substr(std::min(args.find(' ') + 1, args.size()));
        std::size_t s1 = args.find(' ');
        if (s1 == std::string::npos) return;
        std::size_t s2 = args.find(' ', s1 + 1);
        std::string table = args.substr(0, s1);
        std::string item = args.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
        std::string data = s2 == std::string::npos ? std::string() : args.substr(s2 + 1);
        if (!i.hashExists(table)) i.hashMake(table);
        i.hashSet(table, item, data);
    });
    in.registerCommand("hdel", [](Interpreter& i, S& s, const std::string& a) {
        std::string args = i.evaluate(a, s);
        std::size_t sp = args.find(' ');
        if (sp == std::string::npos) return;
        i.hashDelete(args.substr(0, sp), args.substr(sp + 1));
    });
    auto hincdec = [](bool inc) {
        return [inc](Interpreter& i, S& s, const std::string& a) {
            std::string args = i.evaluate(a, s);
            auto toks = splitTokens(args, ' ');
            if (toks.size() < 2) return;
            double by = toks.size() >= 3 ? [&] { try { return std::stod(toks[2]); } catch (...) { return 1.0; } }() : 1.0;
            double cur = 0;
            try {
                cur = std::stod(i.hashGet(toks[0], toks[1]));
            } catch (...) {
            }
            if (!i.hashExists(toks[0])) i.hashMake(toks[0]);
            i.hashSet(toks[0], toks[1], fmtNum(inc ? cur + by : cur - by));
        };
    };
    in.registerCommand("hinc", hincdec(true));
    in.registerCommand("hdec", hincdec(false));

    // Files and INI.
    in.registerCommand("write", [](Interpreter& i, S& s, const std::string& a) {
        // /write [-c] <file> <text>   (append a line; -c clears first)
        std::string args = i.evaluate(a, s);
        bool clear = false;
        if (!args.empty() && args[0] == '-') {
            clear = args.find('c') != std::string::npos;
            args = args.substr(std::min(args.find(' ') + 1, args.size()));
        }
        std::size_t sp = args.find(' ');
        if (sp == std::string::npos) return;
        const std::string file = args.substr(0, sp);
        const std::string text = args.substr(sp + 1);
        std::string content = clear ? std::string() : i.host().fileRead(file);
        if (!content.empty() && content.back() != '\n') content += '\n';
        content += text + "\n";
        i.host().fileWrite(file, content);
    });
    in.registerCommand("writeini", [](Interpreter& i, S& s, const std::string& a) {
        std::string args = i.evaluate(a, s);
        if (!args.empty() && args[0] == '-') args = args.substr(std::min(args.find(' ') + 1, args.size()));
        auto toks = splitTokens(args, ' ');
        if (toks.size() < 4) return;
        const std::string file = toks[0], section = toks[1], item = toks[2];
        std::string value;
        for (std::size_t k = 3; k < toks.size(); ++k) value += (k > 3 ? " " : "") + toks[k];
        i.host().fileWrite(file, writeIniValue(i.host().fileRead(file), section, item, value));
    });
    in.registerCommand("remini", [](Interpreter& i, S& s, const std::string& a) {
        // Minimal: /remini removes by rewriting the value empty (full section
        // removal is a later refinement).
        auto toks = splitTokens(i.evaluate(a, s), ' ');
        if (toks.size() >= 3) i.host().fileWrite(toks[0], writeIniValue(i.host().fileRead(toks[0]), toks[1], toks[2], ""));
    });
    in.registerCommand("mkdir", [](Interpreter& i, S& s, const std::string& a) { i.host().makeDir(i.evaluate(a, s)); });
    in.registerCommand("remove", [](Interpreter& i, S& s, const std::string& a) {
        i.host().removePath(i.evaluate(a, s));
    });

    // Dialogs.
    in.registerCommand("dialog", [](Interpreter& i, S& s, const std::string& a) {
        std::string args = i.evaluate(a, s);
        auto toks = splitTokens(args, ' ');
        std::size_t idx = 0;
        std::string sw;
        if (!toks.empty() && !toks[0].empty() && toks[0][0] == '-') sw = toks[idx++];
        if (idx >= toks.size()) return;
        const std::string name = toks[idx++];
        const std::string table = idx < toks.size() ? toks[idx] : name;
        if (sw.find('c') != std::string::npos) {
            i.host().closeDialog(name);
            return;
        }
        if (const DialogTable* t = i.dialogTable(table)) i.host().openDialog(name, *t, sw.find('o') != std::string::npos);
    });
    in.registerCommand("did", [](Interpreter& i, S& s, const std::string& a) {
        std::string args = i.evaluate(a, s);
        auto toks = splitTokens(args, ' ');
        std::size_t idx = 0;
        std::string sw;
        if (!toks.empty() && !toks[0].empty() && toks[0][0] == '-') sw = toks[idx++].substr(1);
        if (idx + 1 >= toks.size()) return;
        const std::string name = toks[idx++];
        const int id = [&] { try { return std::stoi(toks[idx]); } catch (...) { return 0; } }();
        ++idx;
        // Remaining tokens are the text/argument (a leading line number is kept in it).
        std::string text;
        for (std::size_t k = idx; k < toks.size(); ++k) text += (k > idx ? " " : "") + toks[k];
        i.host().didCommand(name, id, sw, text);
    });

    in.registerCommand("alias", [](Interpreter& i, S&, const std::string& args) {
        // /alias [-l] <name> <command...>  (the -l local flag is accepted, no-op here)
        std::string a = args;
        if (a.rfind("-", 0) == 0) {
            std::size_t sp = a.find(' ');
            a = sp == std::string::npos ? std::string() : a.substr(sp + 1);
        }
        std::size_t sp = a.find(' ');
        if (sp == std::string::npos) return;
        i.defineAlias(a.substr(0, sp), a.substr(sp + 1));
    });
    in.registerCommand("timer", [](Interpreter& i, S&, const std::string& args) {
        // /timer[name] <repeat> <interval> <command>
        auto toks = splitTokens(args, ' ');
        if (toks.size() < 3) return;
        int repeat = 0;
        double interval = 0;
        try {
            repeat = std::stoi(toks[0]);
            interval = std::stod(toks[1]);
        } catch (...) {
            return;
        }
        std::string cmd;
        for (std::size_t k = 2; k < toks.size(); ++k) cmd += (k > 2 ? " " : "") + toks[k];
        i.host().startTimer(std::string(), repeat, interval, cmd);
    });
}

}  // namespace lurker::msl
