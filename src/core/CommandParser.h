// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Turns a line typed into the input box into the actions a Lurker client sends.
// The server does not interpret "/" in message text (docs/CLIENT_PROTOCOL.md
// section 12), so slash commands are parsed here and mapped onto typed verbs,
// with a raw IRC line as the fallback for everything else.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace lurker {

enum class BufferKind { Channel, Dm, Server, System };

// Where the line was typed.
struct ParseContext {
    BufferKind kind = BufferKind::Server;
    std::string target;                // channel or nick; empty for server/system buffers
    std::string chanTypes = "#&+!";    // ISUPPORT CHANTYPES (network-specific)
};

enum class ActionKind {
    Send,          // PRIVMSG: target, text
    Action,        // CTCP ACTION (/me): target, text
    Notice,        // NOTICE: target, text
    Raw,           // raw IRC line: line (echo=false for credential commands)
    Join,          // channel, key
    Part,          // channel, reason
    OpenBuffer,    // open or create a DM/channel buffer: target
    CloseBuffer,   // target
    SetTopic,      // channel, text (empty text clears)
    Away,          // text
    Back,
    Ctcp,          // target, ctcpType, text (arguments)
    ListChannels,
    LocalClear,    // clear the on-screen buffer only
    LocalHelp,     // show the command list
};

struct Action {
    ActionKind kind = ActionKind::Send;
    std::string target;
    std::string channel;
    std::string text;
    std::string line;
    std::string key;
    std::string reason;
    std::string ctcpType;
    bool echo = true;  // false: do not show the line locally (passwords)
    bool operator==(const Action&) const = default;
};

struct ParseResult {
    std::vector<Action> actions;
    std::string error;  // non-empty when the line could not be turned into actions
    bool ok() const { return error.empty(); }
};

ParseResult parseInput(std::string_view line, const ParseContext& ctx);

// One line per supported command, for /help.
std::string helpText();

// True when `name` starts with one of `chanTypes`.
bool isChannelName(std::string_view name, std::string_view chanTypes);

}  // namespace lurker
