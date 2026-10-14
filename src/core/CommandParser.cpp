// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/CommandParser.h"

#include <algorithm>
#include <cctype>

namespace lurker {

namespace {

struct Words {
    std::string head;
    std::string tail;
};

// Splits off the first space-delimited word; `tail` has its leading spaces removed.
Words popWord(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && s[start] == ' ') ++start;
    std::size_t end = start;
    while (end < s.size() && s[end] != ' ') ++end;
    std::size_t rest = end;
    while (rest < s.size() && s[rest] == ' ') ++rest;
    return {std::string(s.substr(start, end - start)), std::string(s.substr(rest))};
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        out.push_back(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return out;
}

bool isConversation(const ParseContext& ctx) {
    return (ctx.kind == BufferKind::Channel || ctx.kind == BufferKind::Dm) && !ctx.target.empty();
}

ParseResult fail(std::string message) {
    ParseResult r;
    r.error = std::move(message);
    return r;
}

ParseResult one(Action a) {
    ParseResult r;
    r.actions.push_back(std::move(a));
    return r;
}

ParseResult raw(std::string line, bool echo = true) {
    Action a;
    a.kind = ActionKind::Raw;
    a.line = std::move(line);
    a.echo = echo;
    return one(std::move(a));
}

ParseResult say(const std::string& text, const ParseContext& ctx) {
    if (!isConversation(ctx)) return fail("You can't send messages here. Use /join or /query first.");
    Action a;
    a.kind = ActionKind::Send;
    a.target = ctx.target;
    a.text = text;
    return one(std::move(a));
}

// A message-like command with an explicit target: /msg, /notice, /describe.
ParseResult targeted(ActionKind kind, const std::string& usage, const std::string& args) {
    const Words w = popWord(args);
    if (w.head.empty() || w.tail.empty()) return fail("Usage: " + usage);
    Action a;
    a.kind = kind;
    a.target = w.head;
    a.text = w.tail;
    return one(std::move(a));
}

ParseResult joinCommand(const std::string& args, const ParseContext& ctx) {
    const Words w = popWord(args);
    if (w.head.empty()) return fail("Usage: /join <#channel>[,<#channel>...] [key[,key...]]");
    const Words k = popWord(w.tail);
    const std::vector<std::string> keys = k.head.empty() ? std::vector<std::string>{} : splitOn(k.head, ',');
    ParseResult r;
    const std::vector<std::string> names = splitOn(w.head, ',');
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i].empty()) continue;
        Action a;
        a.kind = ActionKind::Join;
        a.channel = isChannelName(names[i], ctx.chanTypes) ? names[i] : "#" + names[i];
        if (i < keys.size()) a.key = keys[i];
        r.actions.push_back(std::move(a));
    }
    if (r.actions.empty()) return fail("Usage: /join <#channel>[,<#channel>...] [key[,key...]]");
    return r;
}

// Resolves an optional leading channel argument: "/part #x reason" vs "/part reason".
struct ChannelArg {
    std::string channel;
    std::string rest;
    bool ok = false;
};
ChannelArg channelArg(const std::string& args, const ParseContext& ctx) {
    const Words w = popWord(args);
    if (!w.head.empty() && isChannelName(w.head, ctx.chanTypes)) return {w.head, w.tail, true};
    if (ctx.kind == BufferKind::Channel && !ctx.target.empty()) return {ctx.target, args, true};
    return {};
}

ParseResult partCommand(const std::string& args, const ParseContext& ctx) {
    const ChannelArg c = channelArg(args, ctx);
    if (!c.ok) return fail("Usage: /part [#channel] [reason]");
    Action a;
    a.kind = ActionKind::Part;
    a.channel = c.channel;
    a.reason = c.rest;
    return one(std::move(a));
}

ParseResult topicCommand(const std::string& args, const ParseContext& ctx) {
    const ChannelArg c = channelArg(args, ctx);
    if (!c.ok) return fail("Usage: /topic [#channel] [new topic]");
    if (c.rest.empty()) return raw("TOPIC " + c.channel);  // query
    Action a;
    a.kind = ActionKind::SetTopic;
    a.channel = c.channel;
    a.text = c.rest;
    return one(std::move(a));
}

ParseResult kickCommand(const std::string& args, const ParseContext& ctx) {
    const ChannelArg c = channelArg(args, ctx);
    if (!c.ok) return fail("Usage: /kick [#channel] <nick> [reason]");
    const Words w = popWord(c.rest);
    if (w.head.empty()) return fail("Usage: /kick [#channel] <nick> [reason]");
    return raw("KICK " + c.channel + " " + w.head + (w.tail.empty() ? "" : " :" + w.tail));
}

ParseResult modeCommand(const std::string& args, const ParseContext& ctx) {
    const Words w = popWord(args);
    const bool inChannel = ctx.kind == BufferKind::Channel && !ctx.target.empty();
    if (w.head.empty()) {
        if (!inChannel) return fail("Usage: /mode <target> [modes] [args]");
        return raw("MODE " + ctx.target);
    }
    if ((w.head[0] == '+' || w.head[0] == '-') && inChannel) return raw("MODE " + ctx.target + " " + args);
    return raw("MODE " + args);
}

// /op /deop /voice /devoice: "MODE #chan +ooo a b c", at most three nicks per line.
ParseResult prefixMode(char sign, char letter, const std::string& args, const ParseContext& ctx) {
    const ChannelArg c = channelArg(args, ctx);
    if (!c.ok) return fail(std::string("Usage: /") + (sign == '+' ? "" : "de") + (letter == 'o' ? "op" : "voice") +
                           " [#channel] <nick> [nick...]");
    std::vector<std::string> nicks;
    for (std::string rest = c.rest;;) {
        const Words w = popWord(rest);
        if (w.head.empty()) break;
        nicks.push_back(w.head);
        rest = w.tail;
    }
    if (nicks.empty()) return fail("Give at least one nick.");
    ParseResult r;
    for (std::size_t i = 0; i < nicks.size(); i += 3) {
        const std::size_t n = std::min<std::size_t>(3, nicks.size() - i);
        std::string line = "MODE " + c.channel + " " + sign + std::string(n, letter);
        for (std::size_t j = 0; j < n; ++j) line += " " + nicks[i + j];
        Action a;
        a.kind = ActionKind::Raw;
        a.line = std::move(line);
        r.actions.push_back(std::move(a));
    }
    return r;
}

ParseResult ctcpCommand(const std::string& type, const std::string& args) {
    const Words w = popWord(args);
    if (w.head.empty()) return fail("Usage: /" + lower(type) + " <nick>");
    Action a;
    a.kind = ActionKind::Ctcp;
    a.target = w.head;
    a.ctcpType = type;
    a.text = w.tail;
    return one(std::move(a));
}

// NickServ-style commands carry credentials, so they are sent without a local echo.
ParseResult service(const std::string& serviceName, const std::string& args) {
    if (args.empty()) return fail("Usage: /" + lower(serviceName) + " <command>");
    return raw("PRIVMSG " + serviceName + " :" + args, /*echo=*/false);
}

}  // namespace

bool isChannelName(std::string_view name, std::string_view chanTypes) {
    return !name.empty() && chanTypes.find(name.front()) != std::string_view::npos;
}

ParseResult parseInput(std::string_view line, const ParseContext& ctx) {
    if (line.find_first_of(std::string_view("\r\n\0", 3)) != std::string_view::npos) {
        return fail("Messages can't contain line breaks.");
    }
    // Ignore a line of nothing but spaces.
    if (line.find_first_not_of(' ') == std::string_view::npos) return {};

    // "//text" sends a literal message that starts with a slash.
    if (line.size() >= 2 && line[0] == '/' && line[1] == '/') return say(std::string(line.substr(1)), ctx);

    // A lone "/" or "/ text" is not a command.
    if (line[0] != '/' || line.size() == 1 || line[1] == ' ') return say(std::string(line), ctx);

    const Words cmd = popWord(line.substr(1));
    const std::string name = lower(cmd.head);
    const std::string& args = cmd.tail;

    if (name == "me") {
        if (!isConversation(ctx)) return fail("You can't use /me here.");
        if (args.empty()) return fail("Usage: /me <action>");
        Action a;
        a.kind = ActionKind::Action;
        a.target = ctx.target;
        a.text = args;
        return one(std::move(a));
    }
    if (name == "describe") return targeted(ActionKind::Action, "/describe <target> <action>", args);
    if (name == "msg" || name == "m") return targeted(ActionKind::Send, "/msg <target> <message>", args);
    if (name == "notice") return targeted(ActionKind::Notice, "/notice <target> <message>", args);

    if (name == "query" || name == "q") {
        const Words w = popWord(args);
        if (w.head.empty()) return fail("Usage: /query <nick> [message]");
        if (isChannelName(w.head, ctx.chanTypes)) return fail("That's a channel. Use /join.");
        ParseResult r;
        Action open;
        open.kind = ActionKind::OpenBuffer;
        open.target = w.head;
        r.actions.push_back(std::move(open));
        if (!w.tail.empty()) {
            Action send;
            send.kind = ActionKind::Send;
            send.target = w.head;
            send.text = w.tail;
            r.actions.push_back(std::move(send));
        }
        return r;
    }

    if (name == "join" || name == "j") return joinCommand(args, ctx);
    if (name == "part" || name == "leave") return partCommand(args, ctx);
    if (name == "close") {
        if (!isConversation(ctx)) return fail("There's nothing to close here.");
        Action a;
        a.kind = ActionKind::CloseBuffer;
        a.target = ctx.target;
        return one(std::move(a));
    }
    if (name == "topic") return topicCommand(args, ctx);
    if (name == "kick") return kickCommand(args, ctx);
    if (name == "mode") return modeCommand(args, ctx);
    if (name == "op") return prefixMode('+', 'o', args, ctx);
    if (name == "deop") return prefixMode('-', 'o', args, ctx);
    if (name == "voice") return prefixMode('+', 'v', args, ctx);
    if (name == "devoice") return prefixMode('-', 'v', args, ctx);

    if (name == "nick") {
        const Words w = popWord(args);
        if (w.head.empty()) return fail("Usage: /nick <new nick>");
        return raw("NICK " + w.head);
    }
    if (name == "away") {
        Action a;
        a.kind = ActionKind::Away;
        a.text = args;
        return one(std::move(a));
    }
    if (name == "back") {
        Action a;
        a.kind = ActionKind::Back;
        return one(std::move(a));
    }
    if (name == "whois" || name == "w") {
        std::string who = popWord(args).head;
        if (who.empty() && ctx.kind == BufferKind::Dm) who = ctx.target;
        if (who.empty()) return fail("Usage: /whois <nick>");
        return raw("WHOIS " + who);
    }
    if (name == "invite") {
        const Words w = popWord(args);
        if (w.head.empty()) return fail("Usage: /invite <nick> [#channel]");
        std::string chan = popWord(w.tail).head;
        if (chan.empty() && ctx.kind == BufferKind::Channel) chan = ctx.target;
        if (chan.empty()) return fail("Usage: /invite <nick> [#channel]");
        return raw("INVITE " + w.head + " " + chan);
    }

    if (name == "ctcp") {
        const Words w = popWord(args);
        const Words t = popWord(w.tail);
        if (w.head.empty() || t.head.empty()) return fail("Usage: /ctcp <nick> <type> [args]");
        Action a;
        a.kind = ActionKind::Ctcp;
        a.target = w.head;
        a.ctcpType = upper(t.head);
        a.text = t.tail;
        return one(std::move(a));
    }
    if (name == "ping") return ctcpCommand("PING", args);
    if (name == "version") return ctcpCommand("VERSION", args);
    if (name == "time") return ctcpCommand("TIME", args);

    if (name == "list") {
        Action a;
        a.kind = ActionKind::ListChannels;
        return one(std::move(a));
    }
    if (name == "raw" || name == "quote") {
        if (args.empty()) return fail("Usage: /raw <IRC line>");
        return raw(args);
    }
    if (name == "ns" || name == "nickserv") return service("NickServ", args);
    if (name == "cs" || name == "chanserv") return service("ChanServ", args);
    if (name == "identify") {
        if (args.empty()) return fail("Usage: /identify [account] <password>");
        return service("NickServ", "IDENTIFY " + args);
    }

    if (name == "clear") {
        Action a;
        a.kind = ActionKind::LocalClear;
        return one(std::move(a));
    }
    if (name == "help") {
        Action a;
        a.kind = ActionKind::LocalHelp;
        return one(std::move(a));
    }

    // The bouncer owns the upstream connection, so these would tear down the
    // network for every client the user has attached.
    if (name == "quit" || name == "exit" || name == "server" || name == "disconnect") {
        return fail("/" + name + " isn't available: Lurker keeps your networks connected. "
                                 "Use the network's connect and disconnect controls.");
    }

    // Anything else goes to the server as written ("/who #x" becomes "WHO #x").
    return raw(upper(cmd.head) + (args.empty() ? "" : " " + args));
}

std::string helpText() {
    return "Commands:\n"
           "  /me <action>                 /msg <target> <text>         /notice <target> <text>\n"
           "  /query <nick> [text]         /join <#chan>[,..] [key]     /part [#chan] [reason]\n"
           "  /close                       /topic [#chan] [text]        /nick <nick>\n"
           "  /away [message]              /back                        /whois [nick]\n"
           "  /kick [#chan] <nick> [why]   /mode [target] [modes]       /invite <nick> [#chan]\n"
           "  /op /deop /voice /devoice [#chan] <nick>...               /list\n"
           "  /ctcp <nick> <type> [args]   /ping /version /time <nick>  /raw <line>\n"
           "  /ns /cs <command>            /identify <password>         /clear   /help\n"
           "  //text sends a message that begins with a slash. Unknown /commands are sent to the server as-is.";
}

}  // namespace lurker
