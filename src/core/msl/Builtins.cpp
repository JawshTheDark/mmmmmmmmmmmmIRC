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
