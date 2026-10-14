// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// IrcSession is the transport-free IRC client state machine: feed it the lines
// a server sends and it drives registration (CAP 302, SASL PLAIN), tracks
// ISUPPORT, channels and members, answers PING and CTCP, and reports typed
// IrcEvents through a sink. The Qt backend owns the socket; tests feed lines
// directly. Written from the modern IRC client protocol and the IRCv3
// capability/sasl specifications (https://modern.ircdocs.horse, https://ircv3.net).
#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/IrcEvent.h"

namespace lurker {

struct SessionConfig {
    std::string nick;
    std::string username;        // USER name and SASL authcid (bouncer: "user" or "user/network")
    std::string realname = "Lurker Desktop";
    std::string password;        // SASL PLAIN password; empty = don't negotiate sasl
    std::string serverPassword;  // PASS, sent before registration when non-empty
    std::string ctcpVersionReply = "Lurker Desktop";
};

struct PrefixPair {
    char mode;   // e.g. 'o'
    char sigil;  // e.g. '@'
};

struct Isupport {
    std::string network;
    std::string chanTypes = "#&";
    std::string caseMapping = "rfc1459";
    std::vector<PrefixPair> prefix = {{'o', '@'}, {'v', '+'}};  // highest rank first
    // CHANMODES groups: A = list modes, B = always a parameter,
    // C = parameter only when set, D = no parameter.
    std::string listModes = "beI";
    std::string paramModes = "k";
    std::string setParamModes = "l";
    std::string flagModes;
    std::map<std::string, std::string> tokens;  // every token as sent (value "" for flags)

    int rankOfMode(char mode) const;            // index in prefix; INT_MAX when absent
    char sigilForMode(char mode) const;         // 0 when absent
    char modeForSigil(char sigil) const;        // 0 when absent
};

struct ChannelMember {
    std::string nick;
    std::string user;
    std::string host;
    std::string account;      // "" = none/unknown
    std::string prefixModes;  // prefix-mode letters, highest rank first
    bool away = false;
};

struct ChannelState {
    std::string name;  // display casing from the wire
    std::string topic;
    std::string topicSetBy;
    std::map<std::string, ChannelMember> members;  // key: folded nick
    bool namesPending = false;                     // a NAMES burst is replacing members
};

class IrcSessionSink {
public:
    virtual ~IrcSessionSink() = default;
    virtual void sendLine(const std::string& line) = 0;  // one IRC line, no CRLF
    virtual void onEvent(const IrcEvent& event) = 0;
};

class IrcSession {
public:
    IrcSession(SessionConfig config, IrcSessionSink& sink);

    void start();                          // begins registration (CAP LS 302, NICK, USER)
    void feedLine(std::string_view line);  // one line from the server, CRLF optional

    // Senders. sendMessage/sendAction/sendNotice synthesize the local echo
    // event when the server did not enable echo-message.
    void sendRaw(const std::string& line);
    void sendMessage(const std::string& target, const std::string& text);
    void sendAction(const std::string& target, const std::string& text);
    void sendNotice(const std::string& target, const std::string& text);
    void sendCtcpRequest(const std::string& target, const std::string& type, const std::string& args);
    void join(const std::string& channel, const std::string& key = {});
    void part(const std::string& channel, const std::string& reason = {});
    void setTopic(const std::string& channel, const std::string& topic);
    void setAway(const std::string& message);
    void clearAway();

    bool registered() const { return m_registered; }
    const std::string& nick() const { return m_nick; }
    bool capEnabled(std::string_view name) const;
    const Isupport& isupport() const { return m_isupport; }
    const ChannelState* channel(std::string_view name) const;
    std::vector<std::string> channelNames() const;  // display casing
    std::string casefold(std::string_view s) const; // per CASEMAPPING

private:
    void handle(const IrcMessage& msg);
    void handleCap(const IrcMessage& msg);
    void handleNumeric(int numeric, const IrcMessage& msg);
    void handleNames(const IrcMessage& msg);
    void handleMode(const IrcMessage& msg);
    void handlePrivmsgNotice(const IrcMessage& msg, bool notice);
    void applyIsupportToken(const std::string& token);
    void requestCaps(const std::set<std::string>& available);
    void capEnd();
    void startSasl();
    std::vector<std::string> wantedCaps() const;
    bool isSelf(const IrcSource& source) const;
    bool isChannelName(std::string_view name) const;
    ChannelState* findChannel(std::string_view name);
    void emitEvent(IrcEvent event);
    IrcEvent eventFrom(const IrcMessage& msg, IrcEventType type) const;

    SessionConfig m_config;
    IrcSessionSink& m_sink;
    Isupport m_isupport;
    std::string m_nick;
    bool m_registered = false;
    bool m_capEnded = false;
    bool m_lsDone = false;
    bool m_saslInFlight = false;
    int m_reqOutstanding = 0;
    std::set<std::string> m_capsAvailable;
    std::set<std::string> m_capsEnabled;
    std::map<std::string, ChannelState> m_channels;  // key: folded name
};

}  // namespace lurker
