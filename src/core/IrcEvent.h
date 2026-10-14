// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The internal event model: what an IRC session reports upward to the client.
// Every event keeps the parsed raw line so later layers (and the scripting
// engine) can see exactly what the server sent.
#pragma once

#include <string>

#include "core/IrcMessage.h"

namespace lurker {

enum class IrcEventType {
    Registered,    // 001 received; nick() is confirmed
    SaslOk,        // 903
    SaslFail,      // 904..907; text = server's reason
    Message,       // PRIVMSG text (target = channel, or DM peer)
    Notice,        // NOTICE
    Action,        // CTCP ACTION
    CtcpRequest,   // other CTCP request; arg = type, text = arguments
    CtcpReply,     // CTCP reply in a NOTICE; arg = type, text = arguments
    Join,          // nick joined target
    Part,          // nick left target; text = reason
    Quit,          // nick quit; one event per shared channel; text = reason
    NickChange,    // nick is now arg; one event per shared channel (self: also target "")
    Kick,          // arg was kicked from target by nick; text = reason
    Mode,          // mode change on target; text = modes and params as sent
    Topic,         // nick changed target's topic to text
    TopicState,    // 332/331 reply; text = topic ("" = none)
    NamesUpdated,  // target's member list was replaced (end of NAMES)
    MemberUpdated, // one member changed; arg = what (away/back/account/chghost/setname)
    Invite,        // nick invited us to arg
    MotdLine,      // 375/372 text
    ServerText,    // any other numeric or server-voice text
    ErrorText,     // ERROR or error numerics
    AwaySelf,      // 305/306; arg = "off"/"on"
    CapChanged,    // enabled capability set changed; text = space-joined caps
    Unknown,
};

struct IrcEvent {
    IrcEventType type = IrcEventType::Unknown;
    std::string nick;    // source nick (or server name)
    std::string user;    // source ident
    std::string host;    // source host
    std::string target;  // window the event belongs to; "" = network status
    std::string text;    // main body (message text, reason, topic, modes...)
    std::string arg;     // secondary (kicked nick, new nick, CTCP type, channel invited to)
    std::string account; // IRCv3 account (account-tag / extended-join), "" = none/unknown
    std::string msgid;   // IRCv3 msgid tag, "" = none
    std::string time;    // IRCv3 server-time tag (ISO 8601), "" = none
    bool self = false;   // the source is this connection's user
    int numeric = -1;    // the numeric when the line was one, else -1
    IrcMessage raw;      // the parsed line as received (empty for synthetic events)
};

}  // namespace lurker
