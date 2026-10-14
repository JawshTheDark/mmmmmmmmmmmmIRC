// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "check.h"
#include "core/CommandParser.h"

using namespace lurker;

static ParseContext channel(const std::string& name = "#lurker") {
    ParseContext c;
    c.kind = BufferKind::Channel;
    c.target = name;
    return c;
}
static ParseContext dm(const std::string& nick = "alice") {
    ParseContext c;
    c.kind = BufferKind::Dm;
    c.target = nick;
    return c;
}
static ParseContext server() { return ParseContext{}; }

int main() {
    // Plain text is a message to the current buffer.
    {
        const auto r = parseInput("hello there", channel());
        CHECK(r.ok());
        CHECK_EQ(r.actions.size(), 1u);
        CHECK(r.actions[0].kind == ActionKind::Send);
        CHECK_EQ(r.actions[0].target, std::string("#lurker"));
        CHECK_EQ(r.actions[0].text, std::string("hello there"));
    }
    // Server and system buffers can't take messages.
    CHECK(!parseInput("hello", server()).ok());
    // Blank lines do nothing and are not errors.
    {
        const auto r = parseInput("   ", channel());
        CHECK(r.ok() && r.actions.empty());
    }
    // Line breaks are refused.
    CHECK(!parseInput("a\nb", channel()).ok());
    // "//" sends a literal leading slash; "/ x" and "/" are plain text.
    CHECK_EQ(parseInput("//shrug", channel()).actions[0].text, std::string("/shrug"));
    CHECK_EQ(parseInput("/ not a command", channel()).actions[0].text, std::string("/ not a command"));
    CHECK_EQ(parseInput("/", channel()).actions[0].text, std::string("/"));

    // /me
    {
        const auto r = parseInput("/me waves", dm());
        CHECK(r.ok());
        CHECK(r.actions[0].kind == ActionKind::Action);
        CHECK_EQ(r.actions[0].target, std::string("alice"));
        CHECK_EQ(r.actions[0].text, std::string("waves"));
    }
    CHECK(!parseInput("/me", channel()).ok());
    CHECK(!parseInput("/me hi", server()).ok());

    // /msg keeps the whole remainder as text.
    {
        const auto r = parseInput("/msg bob  hi   there", channel());
        CHECK(r.ok());
        CHECK(r.actions[0].kind == ActionKind::Send);
        CHECK_EQ(r.actions[0].target, std::string("bob"));
        CHECK_EQ(r.actions[0].text, std::string("hi   there"));
    }
    CHECK(!parseInput("/msg bob", channel()).ok());

    // /query opens a buffer and optionally sends.
    {
        const auto r = parseInput("/query bob hey", server());
        CHECK(r.ok());
        CHECK_EQ(r.actions.size(), 2u);
        CHECK(r.actions[0].kind == ActionKind::OpenBuffer);
        CHECK(r.actions[1].kind == ActionKind::Send);
        CHECK_EQ(r.actions[1].text, std::string("hey"));
    }
    CHECK_EQ(parseInput("/query bob", server()).actions.size(), 1u);
    CHECK(!parseInput("/query #chan", server()).ok());

    // /join: several channels with keys, and a missing # prefix.
    {
        const auto r = parseInput("/join #a,b,&c k1,,k3", server());
        CHECK(r.ok());
        CHECK_EQ(r.actions.size(), 3u);
        CHECK_EQ(r.actions[0].channel, std::string("#a"));
        CHECK_EQ(r.actions[0].key, std::string("k1"));
        CHECK_EQ(r.actions[1].channel, std::string("#b"));
        CHECK_EQ(r.actions[1].key, std::string(""));
        CHECK_EQ(r.actions[2].channel, std::string("&c"));
        CHECK_EQ(r.actions[2].key, std::string("k3"));
    }
    CHECK(!parseInput("/join", server()).ok());

    // /part: explicit channel, or the current one.
    {
        const auto a = parseInput("/part #x see you", channel("#y"));
        CHECK_EQ(a.actions[0].channel, std::string("#x"));
        CHECK_EQ(a.actions[0].reason, std::string("see you"));
        const auto b = parseInput("/part bye all", channel("#y"));
        CHECK_EQ(b.actions[0].channel, std::string("#y"));
        CHECK_EQ(b.actions[0].reason, std::string("bye all"));
        CHECK(!parseInput("/part", dm()).ok());
    }

    // /topic: set, query.
    {
        const auto set = parseInput("/topic New topic here", channel());
        CHECK(set.actions[0].kind == ActionKind::SetTopic);
        CHECK_EQ(set.actions[0].text, std::string("New topic here"));
        const auto q = parseInput("/topic", channel());
        CHECK(q.actions[0].kind == ActionKind::Raw);
        CHECK_EQ(q.actions[0].line, std::string("TOPIC #lurker"));
    }

    // /kick
    CHECK_EQ(parseInput("/kick bob spamming", channel()).actions[0].line, std::string("KICK #lurker bob :spamming"));
    CHECK_EQ(parseInput("/kick #z bob", channel()).actions[0].line, std::string("KICK #z bob"));

    // /mode: modes first means "this channel"; otherwise the first word is the target.
    CHECK_EQ(parseInput("/mode +m", channel()).actions[0].line, std::string("MODE #lurker +m"));
    CHECK_EQ(parseInput("/mode #other +b *!*@x", channel()).actions[0].line, std::string("MODE #other +b *!*@x"));
    CHECK_EQ(parseInput("/mode", channel()).actions[0].line, std::string("MODE #lurker"));

    // /op batches three nicks per MODE line.
    {
        const auto r = parseInput("/op a b c d", channel());
        CHECK_EQ(r.actions.size(), 2u);
        CHECK_EQ(r.actions[0].line, std::string("MODE #lurker +ooo a b c"));
        CHECK_EQ(r.actions[1].line, std::string("MODE #lurker +o d"));
        CHECK_EQ(parseInput("/devoice a", channel()).actions[0].line, std::string("MODE #lurker -v a"));
        CHECK(!parseInput("/op", channel()).ok());
        CHECK(!parseInput("/op a", dm()).ok());
    }

    // /nick, /whois, /invite
    CHECK_EQ(parseInput("/nick newname", channel()).actions[0].line, std::string("NICK newname"));
    CHECK_EQ(parseInput("/whois", dm("carol")).actions[0].line, std::string("WHOIS carol"));
    CHECK_EQ(parseInput("/invite dan", channel()).actions[0].line, std::string("INVITE dan #lurker"));

    // /away and /back carry no target.
    CHECK(parseInput("/away lunch", channel()).actions[0].kind == ActionKind::Away);
    CHECK_EQ(parseInput("/away lunch", channel()).actions[0].text, std::string("lunch"));
    CHECK(parseInput("/back", channel()).actions[0].kind == ActionKind::Back);

    // CTCP
    {
        const auto r = parseInput("/ping bob", channel());
        CHECK(r.actions[0].kind == ActionKind::Ctcp);
        CHECK_EQ(r.actions[0].ctcpType, std::string("PING"));
        const auto c = parseInput("/ctcp bob finger now", channel());
        CHECK_EQ(c.actions[0].ctcpType, std::string("FINGER"));
        CHECK_EQ(c.actions[0].text, std::string("now"));
    }

    // Service commands carry passwords: sent raw, never echoed.
    {
        const auto r = parseInput("/ns identify hunter2", channel());
        CHECK(r.ok());
        CHECK_EQ(r.actions[0].line, std::string("PRIVMSG NickServ :identify hunter2"));
        CHECK(!r.actions[0].echo);
        CHECK(!parseInput("/identify hunter2", channel()).actions[0].echo);
    }

    // Unknown commands fall through to the server, uppercased.
    {
        const auto r = parseInput("/who #lurker %na", channel());
        CHECK(r.actions[0].kind == ActionKind::Raw);
        CHECK_EQ(r.actions[0].line, std::string("WHO #lurker %na"));
        CHECK(r.actions[0].echo);
    }
    // Commands that would drop the upstream connection are refused.
    CHECK(!parseInput("/quit bye", channel()).ok());
    CHECK(!parseInput("/server irc.example.net", channel()).ok());

    // Commands are case-insensitive.
    CHECK(parseInput("/JOIN #Foo", server()).actions[0].kind == ActionKind::Join);

    // Local commands.
    CHECK(parseInput("/clear", channel()).actions[0].kind == ActionKind::LocalClear);
    CHECK(parseInput("/help", server()).actions[0].kind == ActionKind::LocalHelp);
    CHECK(!helpText().empty());

    // A network with different CHANTYPES.
    {
        ParseContext c = server();
        c.chanTypes = "#";
        CHECK_EQ(parseInput("/join &x", c).actions[0].channel, std::string("#&x"));
    }

    return check::summary();
}
