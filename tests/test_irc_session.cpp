// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <memory>

#include "check.h"
#include "core/IrcSession.h"

using namespace lurker;

namespace {

struct Harness : IrcSessionSink {
    std::vector<std::string> sent;
    std::vector<IrcEvent> events;
    std::unique_ptr<IrcSession> session;

    explicit Harness(SessionConfig config) { session = std::make_unique<IrcSession>(std::move(config), *this); }

    void sendLine(const std::string& line) override { sent.push_back(line); }
    void onEvent(const IrcEvent& event) override { events.push_back(event); }

    void feed(const std::string& line) { session->feedLine(line); }
    bool sentContains(const std::string& needle) const {
        for (const auto& l : sent) {
            if (l.find(needle) != std::string::npos) return true;
        }
        return false;
    }
    const IrcEvent* last(IrcEventType type) const {
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            if (it->type == type) return &*it;
        }
        return nullptr;
    }
    int count(IrcEventType type) const {
        int n = 0;
        for (const auto& e : events) n += e.type == type;
        return n;
    }
};

SessionConfig config(const std::string& pass = "hunter2") {
    SessionConfig c;
    c.nick = "jawsh";
    c.username = "jawsh";
    c.password = pass;
    return c;
}

// Drives a session through registration with SASL and a Lurker-ish cap set.
void registerSession(Harness& h) {
    h.session->start();
    h.feed(":srv CAP * LS :sasl=PLAIN server-time echo-message message-tags batch multi-prefix userhost-in-names away-notify account-notify extended-join chghost draft/chathistory soju.im/bouncer-networks");
    h.feed(":srv CAP jawsh ACK :sasl server-time echo-message message-tags batch multi-prefix userhost-in-names away-notify account-notify extended-join chghost");
    h.feed("AUTHENTICATE +");
    h.feed(":srv 903 jawsh :SASL authentication successful");
    h.feed(":srv 001 jawsh :Welcome to the network");
    h.feed(":srv 005 jawsh NETWORK=Libera.Chat CASEMAPPING=rfc1459 CHANTYPES=# PREFIX=(qaohv)~&@%+ CHANMODES=beIq,k,l,imnpst :are supported by this server");
}

}  // namespace

int main() {
    // Registration: CAP LS, SASL PLAIN, CAP END, 001, ISUPPORT.
    {
        Harness h(config());
        registerSession(h);
        CHECK_EQ(h.sent[0], std::string("CAP LS 302"));
        CHECK_EQ(h.sent[1], std::string("NICK jawsh"));
        CHECK_EQ(h.sent[2], std::string("USER jawsh 0 * :Lurker Desktop"));
        CHECK(h.sentContains("CAP REQ :"));
        CHECK(h.sentContains("sasl"));
        CHECK(h.sentContains("AUTHENTICATE PLAIN"));
        // base64("jawsh\0jawsh\0hunter2")
        CHECK(h.sentContains("AUTHENTICATE amF3c2gAamF3c2gAaHVudGVyMg=="));
        CHECK(h.sentContains("CAP END"));
        CHECK(h.last(IrcEventType::SaslOk));
        CHECK(h.session->registered());
        CHECK_EQ(h.session->nick(), std::string("jawsh"));
        CHECK(h.session->capEnabled("echo-message"));
        CHECK(!h.session->capEnabled("draft/chathistory"));
        CHECK_EQ(h.session->isupport().network, std::string("Libera.Chat"));
        CHECK_EQ(h.session->isupport().chanTypes, std::string("#"));
        CHECK_EQ(h.session->isupport().prefix.size(), 5u);
        CHECK_EQ(h.session->isupport().sigilForMode('o'), '@');
        CHECK_EQ(h.session->isupport().modeForSigil('%'), 'h');
        CHECK(h.last(IrcEventType::Registered));
    }

    // Multi-line CAP LS; no password -> no sasl requested, still CAP END.
    {
        Harness h(config(""));
        h.session->start();
        h.feed(":srv CAP * LS * :sasl=PLAIN server-time");
        CHECK(!h.sentContains("CAP REQ"));  // waits for the final LS line
        h.feed(":srv CAP * LS :echo-message batch");
        CHECK(h.sentContains("CAP REQ"));
        CHECK(!h.sentContains("sasl "));
        h.feed(":srv CAP * ACK :server-time echo-message batch");
        CHECK(h.sentContains("CAP END"));
    }

    // NAK still ends negotiation; no caps enabled.
    {
        Harness h(config(""));
        h.session->start();
        h.feed(":srv CAP * LS :server-time");
        h.feed(":srv CAP * NAK :server-time");
        CHECK(h.sentContains("CAP END"));
        CHECK(!h.session->capEnabled("server-time"));
    }

    // Nick collision during registration retries with an underscore.
    {
        Harness h(config(""));
        h.session->start();
        h.feed(":srv 433 * jawsh :Nickname is already in use");
        CHECK(h.sentContains("NICK jawsh_"));
        h.feed(":srv 001 jawsh_ :Welcome");
        CHECK_EQ(h.session->nick(), std::string("jawsh_"));
    }

    // PING -> PONG.
    {
        Harness h(config(""));
        h.feed("PING :irc.example.net");
        CHECK(h.sentContains("PONG :irc.example.net"));
    }

    // NAMES with multi-prefix sigils and userhost-in-names; membership and ranks.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":jawsh!j@host JOIN #lurker");
        h.feed(":srv 353 jawsh = #lurker :@+alice!a@ha ~bob +carol jawsh");
        h.feed(":srv 366 jawsh #lurker :End of /NAMES list");
        const ChannelState* chan = h.session->channel("#LURKER");  // casefolded lookup
        CHECK(chan != nullptr);
        CHECK_EQ(chan->members.size(), 4u);
        const ChannelMember& alice = chan->members.at("alice");
        CHECK_EQ(alice.prefixModes, std::string("ov"));
        CHECK_EQ(alice.user, std::string("a"));
        CHECK_EQ(alice.host, std::string("ha"));
        CHECK_EQ(chan->members.at("bob").prefixModes, std::string("q"));
        CHECK(h.last(IrcEventType::NamesUpdated));
    }

    // Join/part/quit/nick/kick bookkeeping.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":jawsh!j@host JOIN #a");
        h.feed(":srv 353 jawsh = #a :jawsh dana");
        h.feed(":srv 366 jawsh #a :done");
        h.feed(":jawsh!j@host JOIN #b");
        h.feed(":srv 353 jawsh = #b :jawsh dana eve");
        h.feed(":srv 366 jawsh #b :done");
        h.feed(":eve!e@h JOIN #a account-name :Real Name");  // extended-join
        CHECK(h.session->channel("#a")->members.count("eve") == 1);
        CHECK_EQ(h.session->channel("#a")->members.at("eve").account, std::string("account-name"));
        h.feed(":dana!d@h QUIT :bye");
        CHECK_EQ(h.count(IrcEventType::Quit), 2);  // once per shared channel
        CHECK(h.session->channel("#a")->members.count("dana") == 0);
        h.feed(":eve!e@h NICK Floyd");
        CHECK(h.session->channel("#b")->members.count("eve") == 0);
        CHECK(h.session->channel("#b")->members.count("floyd") == 1);
        CHECK_EQ(h.session->channel("#b")->members.at("floyd").nick, std::string("Floyd"));
        h.feed(":Floyd!e@h PART #a :gone");
        CHECK(h.session->channel("#a")->members.count("floyd") == 0);
        h.feed(":op!o@h KICK #b Floyd :misbehaving");
        CHECK(h.session->channel("#b")->members.count("floyd") == 0);
        const IrcEvent* kick = h.last(IrcEventType::Kick);
        CHECK_EQ(kick->arg, std::string("Floyd"));
        CHECK_EQ(kick->text, std::string("misbehaving"));
        // Being kicked ourselves drops the channel state.
        h.feed(":op!o@h KICK #a jawsh :out");
        CHECK(h.session->channel("#a") == nullptr);
        // Parting ourselves drops it too.
        h.feed(":jawsh!j@host PART #b");
        CHECK(h.session->channel("#b") == nullptr);
    }

    // Messages: channel, DM retargeting, self echo, tags.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":jawsh!j@host JOIN #lurker");
        h.feed("@time=2026-10-14T12:00:00.000Z;msgid=m1;account=acct :alice!a@h PRIVMSG #lurker :hello all");
        const IrcEvent* msg = h.last(IrcEventType::Message);
        CHECK_EQ(msg->target, std::string("#lurker"));
        CHECK_EQ(msg->text, std::string("hello all"));
        CHECK_EQ(msg->nick, std::string("alice"));
        CHECK_EQ(msg->time, std::string("2026-10-14T12:00:00.000Z"));
        CHECK_EQ(msg->msgid, std::string("m1"));
        CHECK_EQ(msg->account, std::string("acct"));
        CHECK(!msg->self);
        h.feed(":alice!a@h PRIVMSG jawsh :psst");
        CHECK_EQ(h.last(IrcEventType::Message)->target, std::string("alice"));  // DM files under peer
        h.feed(":jawsh!j@host PRIVMSG alice :echoed back");  // echo-message echo of our DM
        const IrcEvent* echo = h.last(IrcEventType::Message);
        CHECK(echo->self);
        CHECK_EQ(echo->target, std::string("alice"));
        h.feed(":alice!a@h PRIVMSG #lurker :\x01" "ACTION waves\x01");
        const IrcEvent* action = h.last(IrcEventType::Action);
        CHECK_EQ(action->text, std::string("waves"));
        // Server notice with no user@host goes to status.
        h.feed(":srv NOTICE jawsh :MOTD missing");
        CHECK_EQ(h.last(IrcEventType::Notice)->target, std::string(""));
    }

    // CTCP VERSION auto-reply; CTCP PING echo.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":alice!a@h PRIVMSG jawsh :\x01VERSION\x01");
        CHECK(h.sentContains("NOTICE alice :\x01VERSION Lurker Desktop\x01"));
        h.feed(":alice!a@h PRIVMSG jawsh :\x01PING 12345\x01");
        CHECK(h.sentContains("NOTICE alice :\x01PING 12345\x01"));
        const IrcEvent* req = h.last(IrcEventType::CtcpRequest);
        CHECK_EQ(req->arg, std::string("PING"));
        h.feed(":alice!a@h NOTICE jawsh :\x01PING 12345\x01");
        CHECK_EQ(h.last(IrcEventType::CtcpReply)->arg, std::string("PING"));
    }

    // Mode changes apply prefix modes with parameters threaded correctly.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":jawsh!j@host JOIN #m");
        h.feed(":srv 353 jawsh = #m :jawsh alice bob");
        h.feed(":srv 366 jawsh #m :done");
        h.feed(":op!o@h MODE #m +ov-o+b alice alice jawsh *!*@bad");
        const ChannelState* chan = h.session->channel("#m");
        CHECK_EQ(chan->members.at("alice").prefixModes, std::string("ov"));
        CHECK_EQ(chan->members.at("jawsh").prefixModes, std::string(""));
        const IrcEvent* mode = h.last(IrcEventType::Mode);
        CHECK_EQ(mode->text, std::string("+ov-o+b alice alice jawsh *!*@bad"));
        // +l takes its parameter only on set; -l consumes none.
        h.feed(":op!o@h MODE #m +lo 5 bob");
        CHECK_EQ(chan->members.at("bob").prefixModes, std::string("o"));
    }

    // Topic state and live topic changes.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":jawsh!j@host JOIN #t");
        h.feed(":srv 332 jawsh #t :old topic");
        CHECK_EQ(h.session->channel("#t")->topic, std::string("old topic"));
        CHECK_EQ(h.last(IrcEventType::TopicState)->text, std::string("old topic"));
        h.feed(":alice!a@h TOPIC #t :new topic");
        CHECK_EQ(h.session->channel("#t")->topic, std::string("new topic"));
        CHECK_EQ(h.session->channel("#t")->topicSetBy, std::string("alice"));
    }

    // away-notify / account-notify / chghost member patches.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":jawsh!j@host JOIN #p");
        h.feed(":srv 353 jawsh = #p :jawsh alice");
        h.feed(":srv 366 jawsh #p :done");
        h.feed(":alice!a@h AWAY :lunch");
        CHECK(h.session->channel("#p")->members.at("alice").away);
        CHECK_EQ(h.last(IrcEventType::MemberUpdated)->arg, std::string("away"));
        h.feed(":alice!a@h AWAY");
        CHECK(!h.session->channel("#p")->members.at("alice").away);
        h.feed(":alice!a@h ACCOUNT newacct");
        CHECK_EQ(h.session->channel("#p")->members.at("alice").account, std::string("newacct"));
        h.feed(":alice!a@h CHGHOST newuser newhost");
        CHECK_EQ(h.session->channel("#p")->members.at("alice").host, std::string("newhost"));
    }

    // Synthetic echo when echo-message is off; none when it is on.
    {
        Harness h(config(""));
        h.session->start();
        h.feed(":srv CAP * LS :server-time");
        h.feed(":srv CAP * ACK :server-time");
        h.feed(":srv 001 jawsh :hi");
        h.session->sendMessage("#x", "hello");
        CHECK(h.sentContains("PRIVMSG #x :hello"));
        const IrcEvent* echo = h.last(IrcEventType::Message);
        CHECK(echo && echo->self);
        CHECK_EQ(echo->target, std::string("#x"));
    }
    {
        Harness h(config());
        registerSession(h);  // echo-message enabled
        h.session->sendMessage("#x", "hello");
        CHECK(h.count(IrcEventType::Message) == 0);
    }

    // rfc1459 casefolding treats []\ and {}| as equal; ascii mapping does not.
    {
        Harness h(config());
        registerSession(h);
        CHECK_EQ(h.session->casefold("Nick[a]\\^"), std::string("nick{a}|~"));
        h.feed(":srv 005 jawsh CASEMAPPING=ascii :are supported");
        CHECK_EQ(h.session->casefold("Nick[a]"), std::string("nick[a]"));
    }

    // Errors route as ErrorText; unknown numerics as ServerText.
    {
        Harness h(config());
        registerSession(h);
        h.feed(":srv 404 jawsh #c :Cannot send to channel");
        const IrcEvent* err = h.last(IrcEventType::ErrorText);
        CHECK_EQ(err->numeric, 404);
        CHECK(err->text.find("Cannot send") != std::string::npos);
        h.feed(":srv 265 jawsh 5 10 :Current local users");
        CHECK(h.last(IrcEventType::ServerText)->numeric == 265);
        h.feed("ERROR :Closing link");
        CHECK_EQ(h.last(IrcEventType::ErrorText)->text, std::string("Closing link"));
    }

    // Senders format the wire correctly.
    {
        Harness h(config());
        registerSession(h);
        h.session->sendAction("#a", "waves");
        CHECK(h.sentContains("PRIVMSG #a :\x01" "ACTION waves\x01"));
        h.session->sendNotice("bob", "hi");
        CHECK(h.sentContains("NOTICE bob :hi"));
        h.session->sendCtcpRequest("bob", "VERSION", "");
        CHECK(h.sentContains("PRIVMSG bob :\x01VERSION\x01"));
        h.session->join("#new", "key1");
        CHECK(h.sentContains("JOIN #new key1"));
        h.session->part("#new", "bye");
        CHECK(h.sentContains("PART #new :bye"));
        h.session->setTopic("#a", "fresh");
        CHECK(h.sentContains("TOPIC #a :fresh"));
        h.session->setAway("food");
        CHECK(h.sentContains("AWAY :food"));
        h.session->clearAway();
        CHECK(h.sent.back() == "AWAY");
    }

    // CAP DEL removes an enabled cap.
    {
        Harness h(config());
        registerSession(h);
        CHECK(h.session->capEnabled("away-notify"));
        h.feed(":srv CAP jawsh DEL :away-notify");
        CHECK(!h.session->capEnabled("away-notify"));
    }

    return check::summary();
}
