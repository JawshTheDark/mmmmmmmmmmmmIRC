// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/msl/Interpreter.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace lurker::msl {

namespace {
constexpr int kMaxDepth = 500;  // recursion guard for aliases/while

bool identChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

void rtrim(std::string& s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
}

std::string trimmed(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
}  // namespace

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool CiLess::operator()(const std::string& a, const std::string& b) const {
    return toLowerAscii(a) < toLowerAscii(b);
}

bool isTrueValue(std::string_view s) {
    if (s.empty()) return false;
    if (s == "0" || s == "$false") return false;
    return true;
}

std::vector<std::string> splitTokens(std::string_view s, char sep) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

Interpreter::Interpreter(Host& host) : m_host(host) { registerBuiltins(*this); }

void Interpreter::registerIdent(const std::string& name, IdentFn fn) { m_idents[name] = std::move(fn); }
void Interpreter::registerCommand(const std::string& name, CommandFn fn) { m_commands[name] = std::move(fn); }
void Interpreter::defineAlias(const std::string& name, const std::string& body) { m_aliases[name] = body; }
bool Interpreter::hasCommand(const std::string& name) const {
    return m_commands.count(name) || m_aliases.count(name);
}
bool Interpreter::hasAlias(const std::string& name) const { return m_aliases.count(name) != 0; }

std::string Interpreter::getVariable(const std::string& name, const Scope& scope) const {
    const auto local = scope.locals.find(name);
    if (local != scope.locals.end()) return local->second;
    const auto global = m_globals.find(name);
    return global != m_globals.end() ? global->second : std::string();
}

void Interpreter::setVariable(const std::string& name, const std::string& value, bool local, Scope& scope) {
    if (local) scope.locals[name] = value;
    else m_globals[name] = value;
}

void Interpreter::unsetVariable(const std::string& name, Scope& scope) {
    scope.locals.erase(name);
    m_globals.erase(name);
}

// --- expression evaluation ----------------------------------------------

std::string Interpreter::evalParam(std::string_view token, Scope& scope) const {
    // token is the text after '$': digits, optionally '-' and more digits, or '-' alone
    if (token == "0") return std::to_string(scope.params.size());
    const std::size_t dash = token.find('-');
    auto at = [&](std::size_t n) -> std::string {
        return (n >= 1 && n <= scope.params.size()) ? scope.params[n - 1] : std::string();
    };
    if (dash == std::string::npos) return at(static_cast<std::size_t>(std::stoul(std::string(token))));
    const std::size_t from = token.substr(0, dash).empty() ? 1 : std::stoul(std::string(token.substr(0, dash)));
    const std::string toStr(token.substr(dash + 1));
    const std::size_t to = toStr.empty() ? scope.params.size() : std::stoul(toStr);
    std::string out;
    for (std::size_t n = from; n <= to && n <= scope.params.size(); ++n) {
        if (!out.empty()) out += ' ';
        out += at(n);
    }
    return out;
}

std::string Interpreter::callIdent(const std::string& name, const std::vector<std::string>& args,
                                   const std::string& prop, Scope& scope) {
    const auto it = m_idents.find(name);
    if (it == m_idents.end()) {
        // Unknown identifier: mIRC leaves it as typed.
        std::string out = "$" + name;
        if (!args.empty()) {
            out += '(';
            for (std::size_t i = 0; i < args.size(); ++i) out += (i ? "," : "") + args[i];
            out += ')';
        }
        if (!prop.empty()) out += "." + prop;
        return out;
    }
    return it->second(*this, scope, args, prop);
}

std::string Interpreter::evaluate(std::string_view text, Scope& scope) {
    std::string out;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '%' && i + 1 < n && identChar(text[i + 1])) {
            std::size_t j = i + 1;
            while (j < n && identChar(text[j])) ++j;
            out += getVariable(std::string(text.substr(i + 1, j - i - 1)), scope);
            i = j;
            continue;
        }
        if (c == '[') {
            // Bracket grouping: evaluate the inside first (higher precedence).
            int depth = 1;
            std::size_t j = i + 1;
            for (; j < n && depth; ++j) {
                if (text[j] == '[') ++depth;
                else if (text[j] == ']') --depth;
            }
            const std::string inner(text.substr(i + 1, j - i - 2));
            out += evaluate(inner, scope);
            i = j;
            continue;
        }
        if (c == '$') {
            std::size_t j = i + 1;
            // $+ concatenation. "$+(a,b,c)" joins its args; bare "$+" drops the
            // surrounding whitespace between two tokens.
            if (j < n && text[j] == '+') {
                if (j + 1 < n && text[j + 1] == '(') {
                    int depth = 1;
                    std::size_t a = j + 2, argStart = a;
                    std::string joined;
                    for (; a < n && depth; ++a) {
                        const char ac = text[a];
                        if (ac == '(') ++depth;
                        else if (ac == ')') {
                            if (--depth == 0) break;
                        } else if (ac == ',' && depth == 1) {
                            joined += evaluate(text.substr(argStart, a - argStart), scope);
                            argStart = a + 1;
                        }
                    }
                    joined += evaluate(text.substr(argStart, a - argStart), scope);
                    out += joined;
                    i = (a < n) ? a + 1 : n;
                    continue;
                }
                rtrim(out);
                i = j + 1;
                while (i < n && text[i] == ' ') ++i;
                continue;
            }
            // $N / $N- / $N-M positional parameters.
            if (j < n && std::isdigit(static_cast<unsigned char>(text[j]))) {
                std::size_t k = j;
                while (k < n && std::isdigit(static_cast<unsigned char>(text[k]))) ++k;
                if (k < n && text[k] == '-') {
                    ++k;
                    while (k < n && std::isdigit(static_cast<unsigned char>(text[k]))) ++k;
                }
                out += evalParam(text.substr(j, k - j), scope);
                i = k;
                continue;
            }
            // $identifier[(args)][.prop]
            std::size_t k = j;
            while (k < n && identChar(text[k])) ++k;
            if (k == j) {  // a lone '$'
                out += '$';
                i = j;
                continue;
            }
            const std::string name(text.substr(j, k - j));
            std::vector<std::string> args;
            if (k < n && text[k] == '(') {
                int depth = 1;
                std::size_t a = k + 1;
                std::size_t argStart = a;
                for (; a < n && depth; ++a) {
                    const char ac = text[a];
                    if (ac == '(') ++depth;
                    else if (ac == ')') {
                        --depth;
                        if (depth == 0) break;
                    } else if (ac == ',' && depth == 1) {
                        args.push_back(evaluate(text.substr(argStart, a - argStart), scope));
                        argStart = a + 1;
                    }
                }
                if (a > argStart || !args.empty()) args.push_back(evaluate(text.substr(argStart, a - argStart), scope));
                k = (a < n) ? a + 1 : n;  // past ')'
            }
            std::string prop;
            if (k < n && text[k] == '.') {
                std::size_t p = k + 1;
                while (p < n && identChar(text[p])) ++p;
                prop = std::string(text.substr(k + 1, p - k - 1));
                k = p;
            }
            out += callIdent(name, args, prop, scope);
            i = k;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

// --- conditions ---------------------------------------------------------

namespace {
bool asNumber(const std::string& s, double& out) {
    if (s.empty()) return false;
    try {
        std::size_t pos = 0;
        out = std::stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

bool wildMatch(std::string_view pat, std::string_view str) {
    // '*' and '?' glob, case-insensitive (mIRC iswm).
    std::size_t p = 0, s = 0, star = std::string::npos, mark = 0;
    auto lc = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    while (s < str.size()) {
        if (p < pat.size() && (pat[p] == '?' || lc(pat[p]) == lc(str[s]))) {
            ++p;
            ++s;
        } else if (p < pat.size() && pat[p] == '*') {
            star = p++;
            mark = s;
        } else if (star != std::string::npos) {
            p = star + 1;
            s = ++mark;
        } else {
            return false;
        }
    }
    while (p < pat.size() && pat[p] == '*') ++p;
    return p == pat.size();
}

bool compare(const std::string& a, const std::string& op, const std::string& b) {
    double na, nb;
    const bool numeric = asNumber(a, na) && asNumber(b, nb);
    if (op == "==") return a == b;
    if (op == "===") return a == b;  // case-sensitive exact (same here; a/b already literal)
    if (op == "!=") return a != b;
    if (op == "<") return numeric ? na < nb : a < b;
    if (op == ">") return numeric ? na > nb : a > b;
    if (op == "<=") return numeric ? na <= nb : a <= b;
    if (op == ">=") return numeric ? na >= nb : a >= b;
    if (op == "//") return nb != 0 && std::fmod(na, nb) == 0;  // a is a multiple of b
    if (op == "isin") return toLowerAscii(b).find(toLowerAscii(a)) != std::string::npos;
    if (op == "isincs") return b.find(a) != std::string::npos;
    if (op == "iswm") return wildMatch(a, b);
    if (op == "iswmcs") return wildMatch(a, b);  // (cs variant; fold not applied — close enough for v1)
    if (op == "isnum") {
        double x;
        return asNumber(a, x);
    }
    return false;
}

bool isOperator(const std::string& t) {
    static const char* ops[] = {"==", "===", "!=", "<",    ">",      "<=",    ">=",
                                "//", "isin", "isincs", "iswm", "iswmcs", "isnum"};
    for (const char* o : ops) {
        if (t == o) return true;
    }
    return false;
}
}  // namespace

bool Interpreter::evalCondition(std::string_view expr, Scope& scope) {
    std::string e = trimmed(expr);
    if (e.empty()) return false;

    // Split on top-level || then &&, respecting parentheses.
    auto splitTop = [](const std::string& s, const std::string& sep) {
        std::vector<std::string> parts;
        int depth = 0;
        std::size_t start = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '(') ++depth;
            else if (s[i] == ')') --depth;
            else if (depth == 0 && s.compare(i, sep.size(), sep) == 0) {
                parts.push_back(s.substr(start, i - start));
                i += sep.size() - 1;
                start = i + 1;
            }
        }
        parts.push_back(s.substr(start));
        return parts;
    };

    const auto orParts = splitTop(e, "||");
    if (orParts.size() > 1) {
        for (const auto& p : orParts) {
            if (evalCondition(p, scope)) return true;
        }
        return false;
    }
    const auto andParts = splitTop(e, "&&");
    if (andParts.size() > 1) {
        for (const auto& p : andParts) {
            if (!evalCondition(p, scope)) return false;
        }
        return true;
    }

    e = trimmed(e);
    if (!e.empty() && e.front() == '!') return !evalCondition(e.substr(1), scope);
    if (!e.empty() && e.front() == '(' && e.back() == ')') return evalCondition(e.substr(1, e.size() - 2), scope);

    // A single comparison: operandA [op operandB]. Tokens are whitespace-split,
    // each evaluated. (Multi-word operands are a documented v1 limitation.)
    const auto toks = splitTokens(e, ' ');
    std::vector<std::string> clean;
    for (const auto& t : toks) {
        if (!t.empty()) clean.push_back(t);
    }
    if (clean.empty()) return false;
    if (clean.size() == 1) return isTrueValue(evaluate(clean[0], scope));
    if (clean.size() >= 3 && isOperator(clean[1])) {
        const std::string a = evaluate(clean[0], scope);
        std::string b;
        for (std::size_t i = 2; i < clean.size(); ++i) b += (i > 2 ? " " : "") + clean[i];
        b = evaluate(b, scope);
        if (clean[1] == "isnum") return compare(a, "isnum", "");
        return compare(a, clean[1], b);
    }
    // Unary word operators like "isnum".
    if (clean.size() == 2 && clean[1] == "isnum") return compare(evaluate(clean[0], scope), "isnum", "");
    return isTrueValue(evaluate(e, scope));
}

// --- statement parsing and execution ------------------------------------

struct Interpreter::Stmt {
    enum class Kind { Command, If, While } kind = Kind::Command;
    std::string text;  // command line (Command) or trailing inline command
    struct Branch {
        std::string cond;              // "" for else
        std::vector<Stmt> body;
    };
    std::vector<Branch> branches;      // If: if/elseif/else; While: single branch
};

namespace {
// Split a script body into top-level pieces separated by newlines or '|',
// keeping { } blocks and ( ) intact.
std::vector<std::string> topLevelPieces(std::string_view s) {
    std::vector<std::string> out;
    int brace = 0, paren = 0;
    std::size_t start = 0;
    auto flush = [&](std::size_t end) {
        std::string piece = trimmed(s.substr(start, end - start));
        if (!piece.empty()) out.push_back(piece);
    };
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{') ++brace;
        else if (c == '}') --brace;
        else if (c == '(') ++paren;
        else if (c == ')') --paren;
        else if ((c == '\n' || (c == '|' && brace == 0 && paren == 0))) {
            if (brace == 0 && paren == 0) {
                flush(i);
                start = i + 1;
            }
        }
    }
    flush(s.size());
    return out;
}

// Extract a (...) group at s[pos]=='('; returns inner, advances pos past ')'.
std::string parseParens(std::string_view s, std::size_t& pos) {
    int depth = 0;
    const std::size_t begin = pos;
    for (; pos < s.size(); ++pos) {
        if (s[pos] == '(') ++depth;
        else if (s[pos] == ')') {
            --depth;
            if (depth == 0) {
                const std::string inner(s.substr(begin + 1, pos - begin - 1));
                ++pos;
                return inner;
            }
        }
    }
    return std::string(s.substr(begin));
}

// Extract a { ... } block at the first '{' at/after pos; advances pos past '}'.
std::string parseBrace(std::string_view s, std::size_t& pos) {
    while (pos < s.size() && s[pos] != '{') ++pos;
    int depth = 0;
    const std::size_t begin = pos;
    for (; pos < s.size(); ++pos) {
        if (s[pos] == '{') ++depth;
        else if (s[pos] == '}') {
            --depth;
            if (depth == 0) {
                const std::string inner(s.substr(begin + 1, pos - begin - 1));
                ++pos;
                return inner;
            }
        }
    }
    return std::string(s.substr(begin + 1));
}

std::string firstWord(const std::string& s) {
    const std::size_t sp = s.find_first_of(" \t");
    return sp == std::string::npos ? s : s.substr(0, sp);
}
}  // namespace

std::vector<Interpreter::Stmt> Interpreter::parseBlock(std::string_view text) const {
    std::vector<Stmt> stmts;
    for (const std::string& piece : topLevelPieces(text)) {
        std::string word = toLowerAscii(firstWord(piece));
        if (word == "if" || word == "while") {
            Stmt st;
            st.kind = word == "if" ? Stmt::Kind::If : Stmt::Kind::While;
            std::size_t pos = piece.find('(');
            if (pos == std::string::npos) continue;
            Stmt::Branch br;
            br.cond = parseParens(piece, pos);
            const std::string block = parseBrace(piece, pos);
            br.body = parseBlock(block);
            st.branches.push_back(std::move(br));
            // elseif / else chain (for if)
            while (st.kind == Stmt::Kind::If) {
                std::string rest = trimmed(piece.substr(std::min(pos, piece.size())));
                std::string rw = toLowerAscii(firstWord(rest));
                if (rw == "elseif") {
                    std::size_t p2 = piece.find('(', pos);
                    if (p2 == std::string::npos) break;
                    pos = p2;
                    Stmt::Branch eb;
                    eb.cond = parseParens(piece, pos);
                    eb.body = parseBlock(parseBrace(piece, pos));
                    st.branches.push_back(std::move(eb));
                } else if (rw == "else") {
                    Stmt::Branch eb;
                    eb.body = parseBlock(parseBrace(piece, pos));
                    st.branches.push_back(std::move(eb));
                    break;
                } else {
                    break;
                }
            }
            stmts.push_back(std::move(st));
        } else {
            Stmt st;
            st.kind = Stmt::Kind::Command;
            st.text = piece;
            stmts.push_back(std::move(st));
        }
    }
    return stmts;
}

void Interpreter::runStmts(const std::vector<Stmt>& stmts, Scope& scope) {
    for (const Stmt& st : stmts) {
        if (scope.halted || scope.returned) return;
        switch (st.kind) {
            case Stmt::Kind::Command:
                runCommandLine(st.text, scope);
                break;
            case Stmt::Kind::If:
                for (const auto& br : st.branches) {
                    if (br.cond.empty() || evalCondition(br.cond, scope)) {
                        runStmts(br.body, scope);
                        break;
                    }
                }
                break;
            case Stmt::Kind::While: {
                const auto& br = st.branches.front();
                int guard = 0;
                while (evalCondition(br.cond, scope)) {
                    runStmts(br.body, scope);
                    if (scope.halted || scope.returned) return;
                    if (++guard > 1000000) {
                        m_host.scriptError("while: loop limit exceeded");
                        break;
                    }
                }
                break;
            }
        }
    }
}

void Interpreter::runCommandLine(std::string_view line, Scope& scope) {
    // mIRC evaluates a command's parameters at execution, not the whole line up
    // front — so a variable NAME argument (/set %x ...) stays literal. We strip
    // the command verb from the raw text and hand each command its raw args;
    // commands evaluate what they need via evaluate(). "//" forces one extra
    // evaluation of the args first.
    std::string s = trimmed(line);
    if (s.empty()) return;
    bool force = false;
    if (s.size() >= 2 && s[0] == '/' && s[1] == '/') {
        s = trimmed(s.substr(2));
        force = true;
    } else if (!s.empty() && s[0] == '/') {
        s = trimmed(s.substr(1));
    }
    if (s.empty()) return;

    const std::string name = toLowerAscii(firstWord(s));
    std::string args = trimmed(s.substr(std::min(firstWord(s).size(), s.size())));
    if (force) args = evaluate(args, scope);

    const auto cmd = m_commands.find(name);
    if (cmd != m_commands.end()) {
        cmd->second(*this, scope, args);
        return;
    }
    const auto alias = m_aliases.find(name);
    if (alias != m_aliases.end()) {
        std::vector<std::string> params;
        for (const auto& t : splitTokens(evaluate(args, scope), ' ')) {
            if (!t.empty()) params.push_back(t);
        }
        callAlias(name, params);
        return;
    }
    // Unknown command: evaluate and hand the line to the host as a server command.
    m_host.sendRaw(m_host.activeTarget().networkId, evaluate(s, scope));
}

void Interpreter::execute(std::string_view script, Scope& scope) {
    if (++scope.depth > kMaxDepth) {
        m_host.scriptError("recursion limit exceeded");
        --scope.depth;
        return;
    }
    runStmts(parseBlock(script), scope);
    --scope.depth;
}

std::string Interpreter::callAlias(const std::string& name, const std::vector<std::string>& params) {
    const auto it = m_aliases.find(name);
    if (it == m_aliases.end()) return std::string();
    Scope scope;
    scope.params = params;
    execute(it->second, scope);
    return scope.returnValue;
}

}  // namespace lurker::msl
