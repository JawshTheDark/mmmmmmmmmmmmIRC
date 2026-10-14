// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The mIRC Scripting Language (MSL) interpreter. Qt-free and host-independent:
// it evaluates identifiers/variables, runs commands, control flow and aliases,
// and reaches the outside world (printing, sending, timers, files, the IRC
// connection) only through the Host interface. Written from mIRC's published
// scripting documentation; behaviour the docs leave open is pinned by the
// oracle (see tools/oracle). This file is the engine core; built-in identifiers
// and commands register themselves in Builtins.cpp.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/msl/Host.h"

namespace lurker::msl {

// Case-insensitive key compare for variables, aliases, identifiers (ASCII fold).
struct CiLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

// Per-invocation state: the positional parameters ($1..$N, $0) and local
// variables (/var). Globals live in the Interpreter.
struct Scope {
    std::vector<std::string> params;              // $1 == params[0]
    std::map<std::string, std::string, CiLess> locals;
    std::string event;                            // $event (the active on-event name)
    bool halted = false;                          // /halt stops the whole script
    bool returned = false;                        // /return stops the current alias
    std::string returnValue;                      // value of /return or /returnex
    int depth = 0;                                // recursion guard
};

class Interpreter {
public:
    explicit Interpreter(Host& host);

    using IdentFn = std::function<std::string(Interpreter&, Scope&, const std::vector<std::string>& args,
                                              const std::string& prop)>;
    using CommandFn = std::function<void(Interpreter&, Scope&, const std::string& args)>;

    void registerIdent(const std::string& name, IdentFn fn);
    void registerCommand(const std::string& name, CommandFn fn);
    void defineAlias(const std::string& name, const std::string& body);
    bool hasCommand(const std::string& name) const;

    // Resolve all $identifiers and %variables in `text` (one pass, like mIRC).
    std::string evaluate(std::string_view text, Scope& scope);
    // Run one or more commands (separated by '|' / newlines, with { } blocks).
    void execute(std::string_view script, Scope& scope);
    // Call an alias by name with already-split parameters; returns its /return value.
    std::string callAlias(const std::string& name, const std::vector<std::string>& params);

    // Condition test for if/while, e.g. "%a == 3" or "x isin $1-".
    bool evalCondition(std::string_view expr, Scope& scope);

    Host& host() { return m_host; }
    std::string getVariable(const std::string& name, const Scope& scope) const;
    void setVariable(const std::string& name, const std::string& value, bool local, Scope& scope);
    void unsetVariable(const std::string& name, Scope& scope);
    const std::map<std::string, std::string, CiLess>& globals() const { return m_globals; }

private:
    friend void registerBuiltins(Interpreter&);

    struct Stmt;
    std::vector<Stmt> parseBlock(std::string_view text) const;
    void runStmts(const std::vector<Stmt>& stmts, Scope& scope);
    void runCommandLine(std::string_view line, Scope& scope);

    std::string evalParam(std::string_view token, Scope& scope) const;  // $1,$0,$1-,$2-3
    std::string callIdent(const std::string& name, const std::vector<std::string>& args, const std::string& prop,
                          Scope& scope);

    Host& m_host;
    std::map<std::string, IdentFn, CiLess> m_idents;
    std::map<std::string, CommandFn, CiLess> m_commands;
    std::map<std::string, std::string, CiLess> m_aliases;
    std::map<std::string, std::string, CiLess> m_globals;
};

// Registers the standard identifier and command set (Builtins.cpp).
void registerBuiltins(Interpreter&);

// Shared helpers used across the engine and builtins.
std::string toLowerAscii(std::string_view s);
std::vector<std::string> splitTokens(std::string_view s, char sep);  // mIRC $gettok semantics helper
bool isTrueValue(std::string_view s);  // non-empty and not "0" / "$false"

}  // namespace lurker::msl
