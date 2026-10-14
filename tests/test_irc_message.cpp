// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "check.h"
#include "core/IrcMessage.h"

using namespace lurker;

int main() {
    // Full line: tags, source, command, middle and trailing params.
    {
        const auto m = IrcMessage::parse("@time=2026-10-14T08:00:00.000Z;msgid=abc :nick!user@host PRIVMSG #chan :hello world\r\n");
        CHECK(m.has_value());
        CHECK_EQ(m->tags.size(), 2u);
        CHECK_EQ(*m->tag("time"), std::string("2026-10-14T08:00:00.000Z"));
        CHECK_EQ(*m->tag("msgid"), std::string("abc"));
        CHECK_EQ(m->source, std::string("nick!user@host"));
        CHECK_EQ(m->command, std::string("PRIVMSG"));
        CHECK_EQ(m->params.size(), 2u);
        CHECK_EQ(m->param(0), std::string("#chan"));
        CHECK_EQ(m->param(1), std::string("hello world"));
        CHECK_EQ(m->param(5), std::string(""));
        const IrcSource s = m->parsedSource();
        CHECK_EQ(s.nick, std::string("nick"));
        CHECK_EQ(s.user, std::string("user"));
        CHECK_EQ(s.host, std::string("host"));
    }

    // No tags, no source, command is upper-cased.
    {
        const auto m = IrcMessage::parse("ping :irc.example.net");
        CHECK(m.has_value());
        CHECK_EQ(m->command, std::string("PING"));
        CHECK(m->source.empty());
        CHECK_EQ(m->param(0), std::string("irc.example.net"));
    }

    // A server source has no user or host.
    {
        const auto m = IrcMessage::parse(":irc.example.net 001 me :Welcome");
        const IrcSource s = m->parsedSource();
        CHECK_EQ(s.nick, std::string("irc.example.net"));
        CHECK(s.user.empty() && s.host.empty());
        CHECK_EQ(m->command, std::string("001"));
    }
    // nick@host without a user.
    {
        const IrcSource s = IrcMessage::parse(":nick@host JOIN #c")->parsedSource();
        CHECK_EQ(s.nick, std::string("nick"));
        CHECK(s.user.empty());
        CHECK_EQ(s.host, std::string("host"));
    }

    // Empty trailing parameter and a trailing parameter that starts with ':'.
    {
        const auto m = IrcMessage::parse("PRIVMSG #c :");
        CHECK_EQ(m->params.size(), 2u);
        CHECK_EQ(m->param(1), std::string(""));
        const auto n = IrcMessage::parse("PRIVMSG #c ::-)");
        CHECK_EQ(n->param(1), std::string(":-)"));
    }

    // Repeated spaces between parameters.
    {
        const auto m = IrcMessage::parse(":s  MODE   #c  +o   bob");
        CHECK_EQ(m->params.size(), 3u);
        CHECK_EQ(m->param(2), std::string("bob"));
    }

    // Empty and source-only lines are rejected.
    CHECK(!IrcMessage::parse("").has_value());
    CHECK(!IrcMessage::parse("\r\n").has_value());
    CHECK(!IrcMessage::parse(":source.only").has_value());

    // Tag value escapes.
    {
        const auto m = IrcMessage::parse("@a=semi\\:colon\\sspace\\\\back\\r\\n;b;c=;+client/x=1 TAGMSG #c");
        CHECK_EQ(*m->tag("a"), std::string("semi;colon space\\back\r\n"));
        CHECK_EQ(*m->tag("b"), std::string(""));
        CHECK_EQ(*m->tag("c"), std::string(""));
        CHECK_EQ(*m->tag("+client/x"), std::string("1"));
        CHECK(!m->tag("missing").has_value());
    }
    CHECK_EQ(unescapeTagValue("x\\"), std::string("x"));       // lone trailing backslash dropped
    CHECK_EQ(unescapeTagValue("\\q"), std::string("q"));       // unknown escape
    CHECK_EQ(escapeTagValue("a; b\\"), std::string("a\\:\\sb\\\\"));

    // A repeated tag keeps its first position but the last value.
    {
        const auto m = IrcMessage::parse("@a=1;b=2;a=3 PING x");
        CHECK_EQ(m->tags.size(), 2u);
        CHECK_EQ(*m->tag("a"), std::string("3"));
    }

    // Serialization: trailing colon only when needed.
    {
        IrcMessage m;
        m.command = "PRIVMSG";
        m.params = {"#chan", "hi there"};
        CHECK_EQ(m.serialize(), std::string("PRIVMSG #chan :hi there"));
        m.params = {"#chan", "hi"};
        CHECK_EQ(m.serialize(), std::string("PRIVMSG #chan hi"));
        m.params = {"#chan", ""};
        CHECK_EQ(m.serialize(), std::string("PRIVMSG #chan :"));
        m.params = {"#chan", ":)"};
        CHECK_EQ(m.serialize(), std::string("PRIVMSG #chan ::)"));
        m.tags = {{"+draft/reply", "x y"}, {"+typing", ""}};
        m.source = "me!u@h";
        CHECK_EQ(m.serialize(), std::string("@+draft/reply=x\\sy;+typing :me!u@h PRIVMSG #chan ::)"));
    }

    // Round trip.
    {
        const std::string line = "@msgid=a\\sb;time=t :n!u@h PRIVMSG #c :hello : world";
        const auto m = IrcMessage::parse(line);
        CHECK_EQ(m->serialize(), line);
        CHECK(*IrcMessage::parse(m->serialize()) == *m);
    }

    return check::summary();
}
