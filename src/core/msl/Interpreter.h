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
    std::string prop;                             // $prop when an alias is called as an identifier
    bool isIdentCall = false;                      // $isid: alias invoked as $name(...) vs a command
    // Event context ($nick, $chan, ... during an on-event).
    std::string nick;
    std::string newnick;  // $newnick during on NICK
    std::string address;  // nick!user@host
    std::string chan;     // channel the event is on
    std::string target;   // the window ($target): channel, query nick or @window
    std::string v1;                               // $v1 / $v2: the operands of the
    std::string v2;                               // most recent if/while comparison
    bool halted = false;                          // /halt stops the whole script
    bool returned = false;                        // /return stops the current alias
    bool breakLoop = false;                       // /break exits the innermost while
    bool continueLoop = false;                    // /continue restarts the innermost while
    std::string returnValue;                      // value of /return or /returnex
    int depth = 0;                                // recursion guard
};

// A script-defined popup menu item (from a `menu <type> { ... }` remote block).
// A separator has `separator`; a submenu has `children`; a leaf has `command`.
struct MenuItem {
    std::string caption;
    std::string command;
    std::vector<MenuItem> children;
    bool separator = false;
};

// One `on <level>:<EVENT>:<match>:<target>:{ body }` remote definition.
struct EventHandler {
    std::string level = "*";
    std::string match = "*";   // wildcard matched against the event's subject text
    std::string target;        // "#", "?", "@", "*", "" or a specific name
    std::vector<std::string> fields;  // all colon-fields after the EVENT (for on DIALOG etc)
    std::string body;
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
    bool hasAlias(const std::string& name) const;

    // Load a remote script file: parses `alias`, `on`, and top-level comments.
    // Replaces nothing — definitions accumulate (later ones win by name/append).
    void loadScript(std::string_view text);
    void clearScripts();  // forget loaded aliases and events (for /unload or reload)

    // Fire an event: runs every matching `on <EVENT>` handler. `subject` is the
    // text its match wildcard is tested against (e.g. the message for TEXT).
    // `scope` carries $nick/$chan/$target/$1-; the handler body runs in it.
    void raiseEvent(const std::string& event, const std::string& subject, Scope& scope);
    // Fire on DIALOG:<name>:<event>:<id> handlers (sclick, init, edit, close, ...).
    void raiseDialogEvent(const std::string& dialogName, const std::string& devent, int id);
    bool hasEvent(const std::string& event) const;
    const DialogTable* dialogTable(const std::string& name) const;
    // Script-defined popup menu for a type ("nicklist", "channel", "query",
    // "menubar", "status", "@name"); nullptr if the script defined none.
    const std::vector<MenuItem>* popupMenu(const std::string& type) const;
    std::size_t aliasCount() const { return m_aliases.size(); }
    std::size_t eventKindCount() const { return m_events.size(); }
    std::size_t eventHandlerCount() const;

    // Resolve all $identifiers and %variables in `text` (one pass, like mIRC).
    std::string evaluate(std::string_view text, Scope& scope);
    // Run one or more commands (separated by '|' / newlines, with { } blocks).
    void execute(std::string_view script, Scope& scope);
    // Call an alias by name with already-split parameters; returns its /return value.
    std::string callAlias(const std::string& name, const std::vector<std::string>& params);

    // Condition test for if/while, e.g. "%a == 3" or "x isin $1-".
    bool evalCondition(std::string_view expr, Scope& scope);

    Host& host() { return m_host; }

    // Hash tables (/hmake, /hadd, $hget, ...). In-memory, insertion-ordered.
    void hashMake(const std::string& table);
    void hashFree(const std::string& table);
    bool hashExists(const std::string& table) const;
    void hashSet(const std::string& table, const std::string& item, const std::string& data);
    void hashDelete(const std::string& table, const std::string& item);
    std::string hashGet(const std::string& table, const std::string& item) const;
    bool hashHas(const std::string& table, const std::string& item) const;
    int hashCount(const std::string& table) const;
    std::string hashItemByIndex(const std::string& table, int n) const;   // $hget(t,N).item
    std::string hashDataByIndex(const std::string& table, int n) const;   // $hget(t,N).data
    bool aliasDefined(const std::string& name) const { return m_aliases.count(name) != 0; }

    // Binary variables (&name) for /bset and $bvar.
    std::vector<unsigned char>& binVar(const std::string& name) { return m_binvars[name]; }
    const std::vector<unsigned char>* binVarGet(const std::string& name) const {
        const auto it = m_binvars.find(name);
        return it == m_binvars.end() ? nullptr : &it->second;
    }
    void binUnset(const std::string& name) { m_binvars.erase(name); }

    // Regex capture store ($regex fills it, $regml reads it), keyed by name.
    void regexStore(const std::string& name, std::vector<std::string> groups);
    std::string regmlGet(const std::string& name, int n) const;
    int regmlCount(const std::string& name) const;

    int lastSockBytes = 0;  // $sockbr: bytes from the most recent /sockread
    std::string ifMatch;    // $ifmatch: last matched/read text ($read, if-wildcard)
    int readN = 0;          // $readn: line number from the most recent $read match

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
    std::map<std::string, std::vector<EventHandler>, CiLess> m_events;

    // Hash tables: ordered items so $hget(table, N) is stable.
    struct HashTable {
        std::vector<std::pair<std::string, std::string>> items;  // insertion order
        std::map<std::string, std::size_t, CiLess> index;        // folded item -> position
    };
    std::map<std::string, HashTable, CiLess> m_hash;
    std::map<std::string, std::vector<std::string>, CiLess> m_regml;  // $regml captures by name
    std::map<std::string, DialogTable, CiLess> m_dialogs;             // dialog tables by name
    std::map<std::string, std::vector<MenuItem>, CiLess> m_menus;     // popup menus by type
    std::map<std::string, std::vector<unsigned char>, CiLess> m_binvars;  // &binary variables
    int m_callDepth = 0;  // guards alias/identifier recursion across scopes
};

// Case-insensitive '*'/'?' wildcard match (mIRC iswm), shared across the engine.
bool matchWild(std::string_view pattern, std::string_view text);

// Registers the standard identifier and command set (Builtins.cpp).
void registerBuiltins(Interpreter&);

// Shared helpers used across the engine and builtins.
std::string toLowerAscii(std::string_view s);
std::vector<std::string> splitTokens(std::string_view s, char sep);  // mIRC $gettok semantics helper
bool isTrueValue(std::string_view s);  // non-empty and not "0" / "$false"

}  // namespace lurker::msl
