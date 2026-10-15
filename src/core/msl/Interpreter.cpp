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

// --- hash tables --------------------------------------------------------

void Interpreter::hashMake(const std::string& table) { m_hash[table]; }
void Interpreter::hashFree(const std::string& table) { m_hash.erase(table); }
bool Interpreter::hashExists(const std::string& table) const { return m_hash.count(table) != 0; }

void Interpreter::hashSet(const std::string& table, const std::string& item, const std::string& data) {
    HashTable& t = m_hash[table];
    const auto it = t.index.find(item);
    if (it != t.index.end()) {
        t.items[it->second].second = data;
    } else {
        t.index[item] = t.items.size();
        t.items.emplace_back(item, data);
    }
}

void Interpreter::hashDelete(const std::string& table, const std::string& item) {
    const auto tit = m_hash.find(table);
    if (tit == m_hash.end()) return;
    HashTable& t = tit->second;
    const auto it = t.index.find(item);
    if (it == t.index.end()) return;
    const std::size_t pos = it->second;
    t.items.erase(t.items.begin() + static_cast<long>(pos));
    t.index.clear();
    for (std::size_t i = 0; i < t.items.size(); ++i) t.index[t.items[i].first] = i;
}

std::string Interpreter::hashGet(const std::string& table, const std::string& item) const {
    const auto tit = m_hash.find(table);
    if (tit == m_hash.end()) return {};
    const auto it = tit->second.index.find(item);
    return it == tit->second.index.end() ? std::string() : tit->second.items[it->second].second;
}

bool Interpreter::hashHas(const std::string& table, const std::string& item) const {
    const auto tit = m_hash.find(table);
    return tit != m_hash.end() && tit->second.index.count(item);
}

int Interpreter::hashCount(const std::string& table) const {
    const auto tit = m_hash.find(table);
    return tit == m_hash.end() ? 0 : static_cast<int>(tit->second.items.size());
}

std::string Interpreter::hashItemByIndex(const std::string& table, int n) const {
    const auto tit = m_hash.find(table);
    if (tit == m_hash.end() || n < 1 || n > static_cast<int>(tit->second.items.size())) return {};
    return tit->second.items[n - 1].first;
}

std::string Interpreter::hashDataByIndex(const std::string& table, int n) const {
    const auto tit = m_hash.find(table);
    if (tit == m_hash.end() || n < 1 || n > static_cast<int>(tit->second.items.size())) return {};
    return tit->second.items[n - 1].second;
}

// --- regex capture store ------------------------------------------------

void Interpreter::regexStore(const std::string& name, std::vector<std::string> groups) {
    m_regml[name] = std::move(groups);
}
std::string Interpreter::regmlGet(const std::string& name, int n) const {
    const auto it = m_regml.find(name);
    if (it == m_regml.end() || n < 1 || n > static_cast<int>(it->second.size())) return {};
    return it->second[n - 1];
}
int Interpreter::regmlCount(const std::string& name) const {
    const auto it = m_regml.find(name);
    return it == m_regml.end() ? 0 : static_cast<int>(it->second.size());
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
        // Custom identifier: $name resolves to an alias of that name, with its
        // args as $1.. and the property available as $prop (mIRC semantics).
        const auto alias = m_aliases.find(name);
        if (alias != m_aliases.end()) {
            if (m_callDepth >= kMaxDepth) {
                m_host.scriptError("recursion limit exceeded in $" + name);
                return {};
            }
            ++m_callDepth;
            Scope aliasScope;
            aliasScope.params = args;
            aliasScope.prop = prop;
            execute(alias->second, aliasScope);
            --m_callDepth;
            return aliasScope.returnValue;
        }
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
            // Bracket grouping forces evaluation of the inside first, but only
            // when there is something to evaluate ($ or %). Plain brackets like
            // a regex "[a-z]" are left literal (matches practical mIRC use).
            int depth = 1;
            std::size_t j = i + 1;
            for (; j < n && depth; ++j) {
                if (text[j] == '[') ++depth;
                else if (text[j] == ']') --depth;
            }
            if (depth == 0) {
                const std::string inner(text.substr(i + 1, j - i - 2));
                if (inner.find('$') != std::string::npos || inner.find('%') != std::string::npos) {
                    out += evaluate(inner, scope);
                    i = j;
                    continue;
                }
            }
            out += c;  // literal '['
            ++i;
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
            // $? / $$? : prompt the user for input ($$? is required — it halts the
            // script on an empty answer). Optional '*' masks input (passwords);
            // optional ="message" or =message sets the prompt. Used heavily in menus.
            if (text[j] == '?' || (text[j] == '$' && j + 1 < n && text[j + 1] == '?')) {
                const bool required = text[j] == '$';
                std::size_t k = j + (required ? 2 : 1);  // move past "?"
                bool masked = false;
                if (k < n && text[k] == '*') {
                    masked = true;
                    ++k;
                }
                std::string prompt;
                if (k < n && text[k] == '=') {
                    ++k;
                    if (k < n && text[k] == '"') {
                        ++k;
                        const std::size_t s = k;
                        while (k < n && text[k] != '"') ++k;
                        prompt = evaluate(text.substr(s, k - s), scope);
                        if (k < n) ++k;  // past the closing quote
                    } else {
                        const std::size_t s = k;
                        while (k < n && text[k] != ' ') ++k;
                        prompt = evaluate(text.substr(s, k - s), scope);
                    }
                }
                const std::string answer = m_host.inputDialog(prompt, masked);
                if (required && answer.empty()) scope.halted = true;
                out += answer;
                i = k;
                continue;
            }
            // $(text,N): the evaluation operator. Evaluates the enclosed text N
            // times (like [ ] brackets, but with an explicit repeat count). N is
            // the final comma-separated argument when it is a non-negative
            // integer; otherwise the whole content is the text and N defaults to
            // 1. N==0 returns the text unevaluated. We take the RAW inner text so
            // the repeated passes see it exactly as written.
            if (j < n && text[j] == '(') {
                int depth = 1;
                std::size_t a = j + 1;
                std::size_t lastComma = std::string_view::npos;
                for (; a < n && depth; ++a) {
                    const char ac = text[a];
                    if (ac == '(') ++depth;
                    else if (ac == ')') {
                        if (--depth == 0) break;
                    } else if (ac == ',' && depth == 1) {
                        lastComma = a;
                    }
                }
                const std::size_t closeParen = a;  // index of ')' or n if unterminated
                std::string_view inner = text.substr(j + 1, closeParen - (j + 1));
                int reps = 1;
                if (lastComma != std::string_view::npos) {
                    std::string_view ntok = text.substr(lastComma + 1, closeParen - lastComma - 1);
                    std::size_t b0 = 0, b1 = ntok.size();
                    while (b0 < b1 && ntok[b0] == ' ') ++b0;
                    while (b1 > b0 && ntok[b1 - 1] == ' ') --b1;
                    ntok = ntok.substr(b0, b1 - b0);
                    const bool numeric = !ntok.empty() &&
                        std::all_of(ntok.begin(), ntok.end(),
                                    [](unsigned char d) { return std::isdigit(d) != 0; });
                    if (numeric) {
                        reps = std::stoi(std::string(ntok));
                        inner = text.substr(j + 1, lastComma - (j + 1));
                    }
                }
                std::string val(inner);
                for (int r = 0; r < reps; ++r) val = evaluate(val, scope);
                out += val;
                i = (closeParen < n) ? closeParen + 1 : n;
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

bool compare(const std::string& a, const std::string& op, const std::string& b) {
    double na, nb;
    const bool numeric = asNumber(a, na) && asNumber(b, nb);
    if (op == "==") return toLowerAscii(a) == toLowerAscii(b);  // mIRC == is case-insensitive
    if (op == "===") return a == b;                             // case-sensitive exact
    if (op == "!=") return toLowerAscii(a) != toLowerAscii(b);  // case-insensitive, opposite of ==
    if (op == "<") return numeric ? na < nb : a < b;
    if (op == ">") return numeric ? na > nb : a > b;
    if (op == "<=") return numeric ? na <= nb : a <= b;
    if (op == ">=") return numeric ? na >= nb : a >= b;
    if (op == "//") return nb != 0 && std::fmod(na, nb) == 0;  // a is a multiple of b
    if (op == "isin") return toLowerAscii(b).find(toLowerAscii(a)) != std::string::npos;
    if (op == "isincs") return b.find(a) != std::string::npos;
    if (op == "iswm") return matchWild(a, b);
    if (op == "iswmcs") return matchWild(a, b);  // (cs variant; fold not applied — close enough for v1)
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
        scope.v1 = a;  // mIRC exposes the operands as $v1/$v2 after a comparison
        scope.v2 = b;
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
    if (pos >= s.size()) return {};  // no block present
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
    return begin + 1 <= s.size() ? std::string(s.substr(begin + 1)) : std::string();
}

// Body after a condition: a { block } or, when none follows, the rest of the
// line (mIRC's braceless `if (x) command`). Advances pos past what it consumed.
std::string parseBody(std::string_view s, std::size_t& pos) {
    while (pos < s.size() && s[pos] == ' ') ++pos;
    if (pos < s.size() && s[pos] == '{') return parseBrace(s, pos);
    const std::size_t begin = std::min(pos, s.size());
    pos = s.size();
    return std::string(s.substr(begin));
}

std::string firstWord(const std::string& s) {
    const std::size_t sp = s.find_first_of(" \t");
    return sp == std::string::npos ? s : s.substr(0, sp);
}
}  // namespace

std::vector<Interpreter::Stmt> Interpreter::parseBlock(std::string_view text) const {
    std::vector<Stmt> stmts;
    const std::vector<std::string> pieces = topLevelPieces(text);

    // Parse a standalone `elseif (cond) body` / `else body` piece onto `st`.
    // Returns true while the chain can continue (after elseif), false after else.
    auto appendElse = [this](Stmt& st, const std::string& piece) -> bool {
        if (toLowerAscii(firstWord(piece)) == "elseif") {
            std::size_t p = piece.find('(');
            if (p == std::string::npos) return false;
            Stmt::Branch eb;
            eb.cond = parseParens(piece, p);
            eb.body = parseBlock(parseBody(piece, p));
            st.branches.push_back(std::move(eb));
            return true;
        }
        const std::size_t ep = piece.find("else");
        std::size_t p = (ep == std::string::npos) ? piece.size() : ep + 4;
        Stmt::Branch eb;
        eb.body = parseBlock(parseBody(piece, p));
        st.branches.push_back(std::move(eb));
        return false;
    };

    for (std::size_t idx = 0; idx < pieces.size(); ++idx) {
        const std::string& piece = pieces[idx];
        const std::string word = toLowerAscii(firstWord(piece));
        if (word == "if" || word == "while") {
            Stmt st;
            st.kind = word == "if" ? Stmt::Kind::If : Stmt::Kind::While;
            std::size_t pos = piece.find('(');
            if (pos == std::string::npos) continue;
            Stmt::Branch br;
            br.cond = parseParens(piece, pos);
            br.body = parseBlock(parseBody(piece, pos));
            st.branches.push_back(std::move(br));
            bool chainOpen = (st.kind == Stmt::Kind::If);
            // elseif / else that trail on the SAME piece (one-line form).
            while (chainOpen) {
                const std::string rest = trimmed(piece.substr(std::min(pos, piece.size())));
                const std::string rw = toLowerAscii(firstWord(rest));
                if (rw == "elseif") {
                    const std::size_t p2 = piece.find('(', pos);
                    if (p2 == std::string::npos) break;
                    pos = p2;
                    Stmt::Branch eb;
                    eb.cond = parseParens(piece, pos);
                    eb.body = parseBlock(parseBody(piece, pos));
                    st.branches.push_back(std::move(eb));
                } else if (rw == "else") {
                    const std::size_t ep = piece.find("else", pos);
                    pos = (ep == std::string::npos) ? piece.size() : ep + 4;
                    Stmt::Branch eb;
                    eb.body = parseBlock(parseBody(piece, pos));
                    st.branches.push_back(std::move(eb));
                    chainOpen = false;
                } else {
                    break;
                }
            }
            // elseif / else that follow as separate pieces (newline- or
            // '|'-separated) — the dominant multi-line mIRC style.
            while (chainOpen && idx + 1 < pieces.size()) {
                const std::string nw = toLowerAscii(firstWord(pieces[idx + 1]));
                if (nw != "elseif" && nw != "else") break;
                ++idx;
                chainOpen = appendElse(st, pieces[idx]);
            }
            stmts.push_back(std::move(st));
        } else if ((word == "elseif" || word == "else") && !stmts.empty() &&
                   stmts.back().kind == Stmt::Kind::If) {
            // Orphaned continuation (its `if` was parsed in a different call);
            // attach to the nearest preceding if rather than mis-running it.
            appendElse(stmts.back(), piece);
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
        // Any of these signals unwinds the current statement list: /halt and
        // /return propagate all the way out, /break and /continue only as far
        // as the enclosing while (which clears them).
        if (scope.halted || scope.returned || scope.breakLoop || scope.continueLoop) return;
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
                    if (scope.breakLoop) {
                        scope.breakLoop = false;
                        break;
                    }
                    scope.continueLoop = false;  // /continue falls through to the next test
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
    // mIRC's silent prefix: a leading '.' runs the command without its normal
    // status output (e.g. .msg, .notice, .timer). We just strip it so the command
    // resolves — our commands don't emit status chatter anyway.
    while (!s.empty() && s[0] == '.') s = trimmed(s.substr(1));
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
    // Global depth guard: a fresh Scope per call means scope.depth can't catch
    // recursion across alias calls, so an unbounded `alias x { x }` would blow
    // the C++ stack. m_callDepth guards every alias/identifier invocation.
    if (m_callDepth >= kMaxDepth) {
        m_host.scriptError("recursion limit exceeded in alias " + name);
        return {};
    }
    ++m_callDepth;
    Scope scope;
    scope.params = params;
    execute(it->second, scope);
    --m_callDepth;
    return scope.returnValue;
}

// --- wildcard + remote scripts ------------------------------------------

bool matchWild(std::string_view pat, std::string_view str) {
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

void Interpreter::clearScripts() {
    m_aliases.clear();
    m_events.clear();
    m_menus.clear();
    m_dialogs.clear();
}

bool Interpreter::hasEvent(const std::string& event) const { return m_events.count(toLowerAscii(event)) != 0; }

std::size_t Interpreter::eventHandlerCount() const {
    std::size_t n = 0;
    for (const auto& [_, v] : m_events) n += v.size();
    return n;
}

namespace {
// mIRC menus nest by LEADING DOTS: a line's dot count is its depth, e.g.
//   Anope              (depth 0, a submenu header — has children below it)
//   .Register:command  (depth 1, under Anope)
//   .ChanServ          (depth 1, a submenu)
//   ..Op:command       (depth 2, under ChanServ)
//   .-                 (a separator at depth 1)
// Captions/commands are kept raw ($N is evaluated when the menu is shown).
struct MenuLine {
    int depth = 0;
    std::string caption;
    std::string command;
    bool sep = false;
};

std::vector<MenuItem> buildMenu(const std::vector<MenuLine>& lines, std::size_t& pos, int depth) {
    std::vector<MenuItem> out;
    while (pos < lines.size()) {
        const MenuLine& ln = lines[pos];
        if (ln.depth < depth) break;  // belongs to a shallower level — return to caller
        if (ln.depth > depth) {       // deeper than expected (jump): attach to the previous item
            if (!out.empty()) {
                std::vector<MenuItem> kids = buildMenu(lines, pos, ln.depth);
                for (auto& k : kids) out.back().children.push_back(std::move(k));
                continue;
            }
        }
        MenuItem item;
        item.caption = ln.caption;
        item.command = ln.command;
        item.separator = ln.sep;
        ++pos;
        if (pos < lines.size() && lines[pos].depth > depth) item.children = buildMenu(lines, pos, depth + 1);
        out.push_back(std::move(item));
    }
    return out;
}

std::vector<MenuItem> parseMenuItems(std::string_view body) {
    std::vector<MenuLine> lines;
    std::size_t i = 0;
    const std::size_t n = body.size();
    while (i < n) {
        const std::size_t ls = i;
        while (i < n && body[i] != '\n') ++i;
        std::string s(body.substr(ls, i - ls));
        if (i < n) ++i;
        std::size_t a = 0;
        while (a < s.size() && (s[a] == ' ' || s[a] == '\t')) ++a;  // strip leading whitespace (not dots)
        s = s.substr(a);
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
        if (s.empty() || s[0] == ';') continue;
        MenuLine ml;
        while (ml.depth < static_cast<int>(s.size()) && s[ml.depth] == '.') ++ml.depth;
        const std::string rest = trimmed(s.substr(ml.depth));
        if (rest.empty()) continue;
        if (rest == "-") {
            ml.sep = true;
        } else {
            const std::size_t c = rest.find(':');
            if (c == std::string::npos) {
                ml.caption = rest;
            } else {
                ml.caption = trimmed(rest.substr(0, c));
                ml.command = trimmed(rest.substr(c + 1));
            }
        }
        lines.push_back(std::move(ml));
    }
    std::size_t pos = 0;
    return buildMenu(lines, pos, 0);
}
}  // namespace

const std::vector<MenuItem>* Interpreter::popupMenu(const std::string& type) const {
    const auto it = m_menus.find(type);
    return it == m_menus.end() ? nullptr : &it->second;
}

void Interpreter::loadScript(std::string_view text) {
    // A remote file is a sequence of definitions. Each is a header line
    // (`alias name ...`, `on <level>:<EVENT>:...:`) followed by either a brace
    // block or, for a one-line alias, the rest of the line. We scan definitions
    // by reading a header up to its '{' (or end of line) and the balanced block.
    std::size_t i = 0;
    const std::size_t n = text.size();
    auto skipWs = [&] {
        while (i < n && (std::isspace(static_cast<unsigned char>(text[i])))) ++i;
    };
    while (i < n) {
        skipWs();
        if (i >= n) break;
        if (text[i] == ';') {  // comment line
            while (i < n && text[i] != '\n') ++i;
            continue;
        }
        // Read the header up to '{' or newline.
        const std::size_t headStart = i;
        while (i < n && text[i] != '{' && text[i] != '\n') ++i;
        std::string header = trimmed(text.substr(headStart, i - headStart));
        std::string body;
        if (i < n && text[i] == '{') {
            std::size_t pos = i;
            body = parseBrace(text, pos);  // balanced { ... }
            i = pos;
        } else {
            // One-line form: body is after the first keyword+name on the header.
        }

        if (header.empty()) continue;
        const std::string kw = toLowerAscii(firstWord(header));
        if (kw == "alias") {
            std::string rest = trimmed(header.substr(5));
            const std::string name = firstWord(rest);
            std::string inlineBody = trimmed(rest.substr(std::min(name.size(), rest.size())));
            defineAlias(name, body.empty() ? inlineBody : body);
        } else if (kw == "dialog") {
            std::string rest = trimmed(header.substr(6));
            const std::string name = firstWord(rest);
            if (!name.empty()) m_dialogs[name] = parseDialogTable(name, body);
        } else if (kw == "menu") {
            // menu <type>[,<type>...] { items }. Accumulate per type.
            const std::string typesStr = trimmed(header.substr(4));
            const std::vector<MenuItem> items = parseMenuItems(body);
            for (const std::string& rawType : splitTokens(typesStr, ',')) {
                const std::string type = toLowerAscii(trimmed(rawType));
                if (type.empty()) continue;
                auto& dst = m_menus[type];
                dst.insert(dst.end(), items.begin(), items.end());
            }
        } else if (kw == "on") {
            // on <level>:<EVENT>:<p1>:<p2>: { body }
            std::vector<std::string> fields = splitTokens(header.substr(0, header.size()), ':');
            // fields[0] = "on <level>", then EVENT, then params...
            if (fields.size() >= 2) {
                EventHandler h;
                h.level = trimmed(fields[0].substr(2));  // after "on"
                const std::string event = toLowerAscii(trimmed(fields[1]));
                for (std::size_t fi = 2; fi < fields.size(); ++fi) {
                    std::string f = trimmed(fields[fi]);
                    if (fi + 1 == fields.size() && f.empty()) break;  // trailing empty before '{'
                    h.fields.push_back(f);
                }
                if (fields.size() >= 4) {
                    h.match = trimmed(fields[2]);
                    h.target = trimmed(fields[3]);
                } else if (fields.size() == 3) {
                    h.target = trimmed(fields[2]);
                }
                if (h.match.empty()) h.match = "*";
                h.body = body;
                m_events[event].push_back(std::move(h));
            }
        }
        // Unknown definitions (menu, dialog, …) are skipped for now.
    }
}

namespace {
bool targetMatches(const std::string& handlerTarget, const Scope& scope) {
    const std::string& t = handlerTarget;
    const bool isChan = !scope.target.empty() && std::string("#&+!").find(scope.target.front()) != std::string::npos;
    if (t.empty() || t == "*") return true;
    if (t == "#") return isChan;
    if (t == "?") return !isChan;
    if (t == "@") return !scope.target.empty() && scope.target.front() == '@';
    // A specific name/wildcard, matched against the channel/target.
    return matchWild(t, scope.target) || matchWild(t, scope.chan);
}
}  // namespace

void Interpreter::raiseEvent(const std::string& event, const std::string& subject, Scope& scope) {
    const auto it = m_events.find(toLowerAscii(event));
    if (it == m_events.end()) return;
    scope.event = event;
    for (const EventHandler& h : it->second) {
        if (!targetMatches(h.target, scope)) continue;
        if (!matchWild(h.match, subject)) continue;
        Scope local = scope;  // each handler gets a fresh copy of the context
        execute(h.body, local);
        if (local.halted) {
            scope.halted = true;
            break;
        }
    }
}

const DialogTable* Interpreter::dialogTable(const std::string& name) const {
    const auto it = m_dialogs.find(name);
    return it == m_dialogs.end() ? nullptr : &it->second;
}

void Interpreter::raiseDialogEvent(const std::string& dialogName, const std::string& devent, int id) {
    const auto it = m_events.find("dialog");
    if (it == m_events.end()) return;
    const std::string idStr = std::to_string(id);
    for (const EventHandler& h : it->second) {
        // fields are [name, event, id] (id optional).
        if (h.fields.size() < 2) continue;
        if (!matchWild(h.fields[0], dialogName)) continue;
        if (!matchWild(h.fields[1], devent)) continue;
        if (h.fields.size() >= 3 && !h.fields[2].empty() && h.fields[2] != "*" && h.fields[2] != idStr) continue;
        Scope scope;
        scope.event = "DIALOG";
        scope.target = dialogName;
        scope.params = {dialogName, devent, idStr};
        execute(h.body, scope);
    }
}

}  // namespace lurker::msl
