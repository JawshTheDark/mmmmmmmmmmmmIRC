// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/IrcSession.h"

#include <algorithm>
#include <climits>
#include <ctime>

namespace lurker {

namespace {

constexpr char kCtcpMarker = '\x01';

std::string toLowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string base64Encode(std::string_view in) {
    static const char* kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size()) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out.push_back(kTable[(v >> 6) & 63]);
        out.push_back(kTable[v & 63]);
        i += 3;
    }
    const std::size_t rest = in.size() - i;
    if (rest == 1) {
        const unsigned v = static_cast<unsigned char>(in[i]) << 16;
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out += "==";
    } else if (rest == 2) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8);
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out.push_back(kTable[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

// Splits "a b c" into words (no empties).
std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') ++i;
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ') ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// "text" -> {type, args} for a CTCP payload without its \x01 markers.
std::pair<std::string, std::string> splitCtcp(std::string_view payload) {
    const std::size_t sp = payload.find(' ');
    std::string type(payload.substr(0, sp));
    for (char& c : type) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return {type, sp == std::string_view::npos ? std::string() : std::string(payload.substr(sp + 1))};
}

}  // namespace

int Isupport::rankOfMode(char mode) const {
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (prefix[i].mode == mode) return static_cast<int>(i);
    }
    return INT_MAX;
}

char Isupport::sigilForMode(char mode) const {
    for (const PrefixPair& p : prefix) {
        if (p.mode == mode) return p.sigil;
    }
    return 0;
}

char Isupport::modeForSigil(char sigil) const {
    for (const PrefixPair& p : prefix) {
        if (p.sigil == sigil) return p.mode;
    }
    return 0;
}

IrcSession::IrcSession(SessionConfig config, IrcSessionSink& sink)
    : m_config(std::move(config)), m_sink(sink), m_nick(m_config.nick) {}

void IrcSession::start() {
    m_sink.sendLine("CAP LS 302");
    if (!m_config.serverPassword.empty()) m_sink.sendLine("PASS :" + m_config.serverPassword);
    m_sink.sendLine("NICK " + m_nick);
    m_sink.sendLine("USER " + (m_config.username.empty() ? m_nick : m_config.username) + " 0 * :" +
                    m_config.realname);
}

void IrcSession::feedLine(std::string_view line) {
    const auto msg = IrcMessage::parse(line);
    if (msg) handle(*msg);
}

std::string IrcSession::casefold(std::string_view s) const {
    std::string out = toLowerAscii(s);
    if (m_isupport.caseMapping == "ascii") return out;
    for (char& c : out) {
        switch (c) {
            case '[': c = '{'; break;
            case ']': c = '}'; break;
            case '\\': c = '|'; break;
            case '^':
                if (m_isupport.caseMapping != "strict-rfc1459") c = '~';
                break;
            default: break;
        }
    }
    return out;
}

bool IrcSession::capEnabled(std::string_view name) const {
    return m_capsEnabled.count(std::string(name)) != 0;
}

const ChannelState* IrcSession::channel(std::string_view name) const {
    const auto it = m_channels.find(casefold(name));
    return it == m_channels.end() ? nullptr : &it->second;
}

ChannelState* IrcSession::findChannel(std::string_view name) {
    const auto it = m_channels.find(casefold(name));
    return it == m_channels.end() ? nullptr : &it->second;
}

std::vector<std::string> IrcSession::channelNames() const {
    std::vector<std::string> out;
    out.reserve(m_channels.size());
    for (const auto& [_, state] : m_channels) out.push_back(state.name);
    return out;
}

bool IrcSession::isSelf(const IrcSource& source) const {
    return !m_nick.empty() && casefold(source.nick) == casefold(m_nick);
}

bool IrcSession::isChannelName(std::string_view name) const {
    return !name.empty() && m_isupport.chanTypes.find(name.front()) != std::string::npos;
}

void IrcSession::emitEvent(IrcEvent event) { m_sink.onEvent(event); }

IrcEvent IrcSession::eventFrom(const IrcMessage& msg, IrcEventType type) const {
    IrcEvent ev;
    ev.type = type;
    const IrcSource src = msg.parsedSource();
    ev.nick = src.nick;
    ev.user = src.user;
    ev.host = src.host;
    ev.self = isSelf(src);
    if (const auto t = msg.tag("time")) ev.time = *t;
    if (const auto t = msg.tag("msgid")) ev.msgid = *t;
    if (const auto t = msg.tag("account")) ev.account = *t;
    ev.raw = msg;
    return ev;
}

std::vector<std::string> IrcSession::wantedCaps() const {
    std::vector<std::string> caps = {
        "message-tags", "server-time", "echo-message", "batch",      "multi-prefix",
        "userhost-in-names", "away-notify", "account-notify", "account-tag", "extended-join",
        "chghost",      "setname",     "invite-notify",  "cap-notify",
    };
    if (!m_config.password.empty()) caps.push_back("sasl");
    return caps;
}

void IrcSession::requestCaps(const std::set<std::string>& available) {
    std::string req;
    for (const std::string& cap : wantedCaps()) {
        if (available.count(cap) && !m_capsEnabled.count(cap)) req += (req.empty() ? "" : " ") + cap;
    }
    if (req.empty()) {
        capEnd();
        return;
    }
    ++m_reqOutstanding;
    m_sink.sendLine("CAP REQ :" + req);
}

void IrcSession::capEnd() {
    if (m_capEnded || m_registered) return;
    m_capEnded = true;
    m_sink.sendLine("CAP END");
}

void IrcSession::startSasl() {
    m_saslInFlight = true;
    m_sink.sendLine("AUTHENTICATE PLAIN");
}

void IrcSession::handleCap(const IrcMessage& msg) {
    // CAP <target> <subcommand> [*] :<caps>
    const std::string sub = msg.param(1);
    const std::string capList = msg.params.empty() ? "" : msg.params.back();
    const bool more = msg.params.size() >= 4 && msg.param(msg.params.size() - 2) == "*";
    if (sub == "LS") {
        for (const std::string& item : words(capList)) {
            m_capsAvailable.insert(item.substr(0, item.find('=')));
        }
        if (!more && !m_lsDone) {
            m_lsDone = true;
            requestCaps(m_capsAvailable);
        }
        return;
    }
    if (sub == "ACK" || sub == "NAK") {
        if (sub == "ACK") {
            for (const std::string& cap : words(capList)) {
                if (!cap.empty() && cap[0] == '-') m_capsEnabled.erase(cap.substr(1));
                else m_capsEnabled.insert(cap);
            }
        }
        if (m_reqOutstanding > 0 && --m_reqOutstanding == 0) {
            if (!m_registered && m_capsEnabled.count("sasl") && !m_config.password.empty()) startSasl();
            else capEnd();
        }
        IrcEvent ev = eventFrom(msg, IrcEventType::CapChanged);
        std::string enabled;
        for (const std::string& cap : m_capsEnabled) enabled += (enabled.empty() ? "" : " ") + cap;
        ev.text = enabled;
        emitEvent(ev);
        return;
    }
    if (sub == "NEW") {
        std::set<std::string> offered;
        for (const std::string& item : words(capList)) offered.insert(item.substr(0, item.find('=')));
        m_capsAvailable.insert(offered.begin(), offered.end());
        requestCaps(offered);
        return;
    }
    if (sub == "DEL") {
        for (const std::string& cap : words(capList)) {
            m_capsAvailable.erase(cap);
            m_capsEnabled.erase(cap);
        }
        IrcEvent ev = eventFrom(msg, IrcEventType::CapChanged);
        std::string enabled;
        for (const std::string& cap : m_capsEnabled) enabled += (enabled.empty() ? "" : " ") + cap;
        ev.text = enabled;
        emitEvent(ev);
        return;
    }
}

void IrcSession::applyIsupportToken(const std::string& token) {
    if (token.empty() || token[0] == '-') return;
    const std::size_t eq = token.find('=');
    const std::string key = token.substr(0, eq);
    const std::string value = eq == std::string::npos ? "" : token.substr(eq + 1);
    m_isupport.tokens[key] = value;
    if (key == "NETWORK") {
        m_isupport.network = value;
    } else if (key == "CHANTYPES") {
        m_isupport.chanTypes = value;
    } else if (key == "CASEMAPPING") {
        m_isupport.caseMapping = value;
    } else if (key == "PREFIX") {
        // "(ov)@+"
        const std::size_t close = value.find(')');
        if (!value.empty() && value[0] == '(' && close != std::string::npos) {
            const std::string modes = value.substr(1, close - 1);
            const std::string sigils = value.substr(close + 1);
            if (modes.size() == sigils.size()) {
                m_isupport.prefix.clear();
                for (std::size_t i = 0; i < modes.size(); ++i) {
                    m_isupport.prefix.push_back({modes[i], sigils[i]});
                }
            }
        }
    } else if (key == "CHANMODES") {
        // "A,B,C,D"
        std::vector<std::string> groups;
        std::size_t start = 0;
        while (groups.size() < 4) {
            const std::size_t comma = value.find(',', start);
            groups.push_back(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        groups.resize(4);
        m_isupport.listModes = groups[0];
        m_isupport.paramModes = groups[1];
        m_isupport.setParamModes = groups[2];
        m_isupport.flagModes = groups[3];
    }
}

void IrcSession::handleNames(const IrcMessage& msg) {
    // 353: <me> <symbol> <channel> :<prefixed names>
    const std::string channelName = msg.param(2);
    ChannelState* state = findChannel(channelName);
    if (!state) {
        auto& slot = m_channels[casefold(channelName)];
        slot.name = channelName;
        state = &slot;
    }
    if (!state->namesPending) {
        state->members.clear();
        state->namesPending = true;
    }
    for (const std::string& item : words(msg.params.empty() ? "" : msg.params.back())) {
        ChannelMember member;
        std::size_t i = 0;
        while (i < item.size()) {
            const char mode = m_isupport.modeForSigil(item[i]);
            if (!mode) break;
            member.prefixModes.push_back(mode);
            ++i;
        }
        std::string name = item.substr(i);
        const std::size_t bang = name.find('!');
        if (bang != std::string::npos) {
            const std::size_t at = name.find('@', bang);
            member.nick = name.substr(0, bang);
            if (at != std::string::npos) {
                member.user = name.substr(bang + 1, at - bang - 1);
                member.host = name.substr(at + 1);
            }
        } else {
            member.nick = name;
        }
        if (member.nick.empty()) continue;
        std::sort(member.prefixModes.begin(), member.prefixModes.end(), [this](char a, char b) {
            return m_isupport.rankOfMode(a) < m_isupport.rankOfMode(b);
        });
        state->members[casefold(member.nick)] = std::move(member);
    }
}

void IrcSession::handleMode(const IrcMessage& msg) {
    const std::string target = msg.param(0);
    if (!isChannelName(target)) {
        IrcEvent ev = eventFrom(msg, IrcEventType::Mode);
        ev.target.clear();
        for (std::size_t i = 1; i < msg.params.size(); ++i) ev.text += (i > 1 ? " " : "") + msg.params[i];
        emitEvent(ev);
        return;
    }
    ChannelState* state = findChannel(target);
    const std::string modes = msg.param(1);
    std::size_t argIndex = 2;
    char sign = '+';
    for (const char c : modes) {
        if (c == '+' || c == '-') {
            sign = c;
            continue;
        }
        const bool isPrefix = m_isupport.rankOfMode(c) != INT_MAX;
        const bool isList = m_isupport.listModes.find(c) != std::string::npos;
        const bool isParam = m_isupport.paramModes.find(c) != std::string::npos;
        const bool isSetParam = m_isupport.setParamModes.find(c) != std::string::npos;
        std::string arg;
        if (isPrefix || isList || isParam || (isSetParam && sign == '+')) arg = msg.param(argIndex++);
        if (isPrefix && state && !arg.empty()) {
            const auto it = state->members.find(casefold(arg));
            if (it != state->members.end()) {
                std::string& pm = it->second.prefixModes;
                const std::size_t pos = pm.find(c);
                if (sign == '+' && pos == std::string::npos) {
                    pm.push_back(c);
                    std::sort(pm.begin(), pm.end(), [this](char a, char b) {
                        return m_isupport.rankOfMode(a) < m_isupport.rankOfMode(b);
                    });
                } else if (sign == '-' && pos != std::string::npos) {
                    pm.erase(pos, 1);
                }
            }
        }
    }
    IrcEvent ev = eventFrom(msg, IrcEventType::Mode);
    ev.target = state ? state->name : target;
    for (std::size_t i = 1; i < msg.params.size(); ++i) ev.text += (i > 1 ? " " : "") + msg.params[i];
    emitEvent(ev);
}

void IrcSession::handlePrivmsgNotice(const IrcMessage& msg, bool notice) {
    IrcEvent ev = eventFrom(msg, notice ? IrcEventType::Notice : IrcEventType::Message);
    std::string target = msg.param(0);
    std::string text = msg.param(1);

    // STATUSMSG: "@#chan" -> the channel window; the sigil stays visible in raw.
    if (target.size() > 1 && m_isupport.modeForSigil(target[0]) && isChannelName(target.substr(1))) {
        target = target.substr(1);
    }

    if (isChannelName(target)) {
        const ChannelState* state = channel(target);
        ev.target = state ? state->name : target;
    } else if (ev.user.empty() && ev.host.empty()) {
        ev.target.clear();  // server-voice text -> status
    } else if (casefold(target) == casefold(m_nick)) {
        ev.target = ev.nick;  // a DM to us files under the peer
    } else {
        ev.target = target;  // our own echoed DM files under the peer too
    }

    if (text.size() >= 2 && text.front() == kCtcpMarker) {
        std::string_view payload(text);
        payload.remove_prefix(1);
        if (!payload.empty() && payload.back() == kCtcpMarker) payload.remove_suffix(1);
        const auto [type, args] = splitCtcp(payload);
        if (!notice && type == "ACTION") {
            ev.type = IrcEventType::Action;
            ev.text = args;
            emitEvent(ev);
            return;
        }
        ev.type = notice ? IrcEventType::CtcpReply : IrcEventType::CtcpRequest;
        ev.arg = type;
        ev.text = args;
        ev.target.clear();  // CTCP traffic reads best in the status window
        emitEvent(ev);
        if (!notice && !ev.self) {
            if (type == "VERSION") {
                sendRaw("NOTICE " + ev.nick + " :\x01VERSION " + m_config.ctcpVersionReply + "\x01");
            } else if (type == "PING") {
                sendRaw("NOTICE " + ev.nick + " :\x01PING" + (args.empty() ? "" : " " + args) + "\x01");
            } else if (type == "TIME") {
                const std::time_t now = std::time(nullptr);
                char buf[64] = {0};
                std::strftime(buf, sizeof buf, "%a %b %d %H:%M:%S %Y", std::localtime(&now));
                sendRaw("NOTICE " + ev.nick + " :\x01TIME " + buf + "\x01");
            } else if (type == "CLIENTINFO") {
                sendRaw("NOTICE " + ev.nick + " :\x01" + "CLIENTINFO ACTION CLIENTINFO PING TIME VERSION\x01");
            }
        }
        return;
    }
    ev.text = text;
    emitEvent(ev);
}

void IrcSession::handleNumeric(int numeric, const IrcMessage& msg) {
    switch (numeric) {
        case 1: {
            m_registered = true;
            if (!msg.params.empty()) m_nick = msg.param(0);
            IrcEvent ev = eventFrom(msg, IrcEventType::Registered);
            ev.text = msg.params.empty() ? "" : msg.params.back();
            emitEvent(ev);
            return;
        }
        case 5: {
            for (std::size_t i = 1; i + 1 < msg.params.size(); ++i) applyIsupportToken(msg.params[i]);
            IrcEvent ev = eventFrom(msg, IrcEventType::ServerText);
            for (std::size_t i = 1; i < msg.params.size(); ++i) ev.text += (i > 1 ? " " : "") + msg.params[i];
            emitEvent(ev);
            return;
        }
        case 305:
        case 306: {
            IrcEvent ev = eventFrom(msg, IrcEventType::AwaySelf);
            ev.arg = numeric == 306 ? "on" : "off";
            ev.text = msg.params.empty() ? "" : msg.params.back();
            emitEvent(ev);
            return;
        }
        case 331:
        case 332: {
            const std::string channelName = msg.param(1);
            ChannelState* state = findChannel(channelName);
            const std::string topic = numeric == 332 ? msg.param(2) : "";
            if (state) state->topic = topic;
            IrcEvent ev = eventFrom(msg, IrcEventType::TopicState);
            ev.target = state ? state->name : channelName;
            ev.text = topic;
            emitEvent(ev);
            return;
        }
        case 333: {
            if (ChannelState* state = findChannel(msg.param(1))) state->topicSetBy = msg.param(2);
            return;
        }
        case 353:
            handleNames(msg);
            return;
        case 366: {
            const std::string channelName = msg.param(1);
            ChannelState* state = findChannel(channelName);
            if (state) state->namesPending = false;
            IrcEvent ev = eventFrom(msg, IrcEventType::NamesUpdated);
            ev.target = state ? state->name : channelName;
            emitEvent(ev);
            return;
        }
        case 375:
        case 372: {
            IrcEvent ev = eventFrom(msg, IrcEventType::MotdLine);
            ev.text = msg.params.empty() ? "" : msg.params.back();
            emitEvent(ev);
            return;
        }
        case 433: {
            if (!m_registered) {
                m_nick += "_";
                m_sink.sendLine("NICK " + m_nick);
            }
            IrcEvent ev = eventFrom(msg, IrcEventType::ErrorText);
            ev.text = "Nickname is already in use" + (m_registered ? std::string() : ", trying " + m_nick);
            emitEvent(ev);
            return;
        }
        case 903: {
            m_saslInFlight = false;
            emitEvent(eventFrom(msg, IrcEventType::SaslOk));
            capEnd();
            return;
        }
        case 904:
        case 905:
        case 906:
        case 907: {
            m_saslInFlight = false;
            IrcEvent ev = eventFrom(msg, IrcEventType::SaslFail);
            ev.text = msg.params.empty() ? "" : msg.params.back();
            emitEvent(ev);
            capEnd();
            return;
        }
        default: {
            IrcEvent ev = eventFrom(msg, numeric >= 400 && numeric < 600 ? IrcEventType::ErrorText
                                                                         : IrcEventType::ServerText);
            ev.numeric = numeric;
            for (std::size_t i = 1; i < msg.params.size(); ++i) ev.text += (i > 1 ? " " : "") + msg.params[i];
            emitEvent(ev);
            return;
        }
    }
}

void IrcSession::handle(const IrcMessage& msg) {
    const std::string& cmd = msg.command;
    if (cmd.size() == 3 && std::all_of(cmd.begin(), cmd.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        const int numeric = (cmd[0] - '0') * 100 + (cmd[1] - '0') * 10 + (cmd[2] - '0');
        handleNumeric(numeric, msg);
        return;
    }
    if (cmd == "PING") {
        m_sink.sendLine("PONG :" + (msg.params.empty() ? "" : msg.params.back()));
        return;
    }
    if (cmd == "CAP") {
        handleCap(msg);
        return;
    }
    if (cmd == "AUTHENTICATE") {
        if (msg.param(0) == "+" && m_saslInFlight) {
            const std::string& user = m_config.username.empty() ? m_config.nick : m_config.username;
            std::string payload = base64Encode(user + '\0' + user + '\0' + m_config.password);
            while (payload.size() >= 400) {
                m_sink.sendLine("AUTHENTICATE " + payload.substr(0, 400));
                payload = payload.substr(400);
            }
            m_sink.sendLine("AUTHENTICATE " + (payload.empty() ? "+" : payload));
        }
        return;
    }
    if (cmd == "ERROR") {
        IrcEvent ev = eventFrom(msg, IrcEventType::ErrorText);
        ev.text = msg.params.empty() ? "" : msg.params.back();
        emitEvent(ev);
        return;
    }
    if (cmd == "PRIVMSG" || cmd == "NOTICE") {
        handlePrivmsgNotice(msg, cmd == "NOTICE");
        return;
    }
    if (cmd == "TAGMSG") return;  // typing indicators arrive here; rendered later
    if (cmd == "JOIN") {
        const std::string channelName = msg.param(0);
        IrcEvent ev = eventFrom(msg, IrcEventType::Join);
        if (msg.params.size() >= 3) ev.account = msg.param(1) == "*" ? "" : msg.param(1);
        if (ev.self) {
            auto& state = m_channels[casefold(channelName)];
            state.name = channelName;
            state.members.clear();
            state.namesPending = false;
            ev.target = state.name;
        } else if (ChannelState* state = findChannel(channelName)) {
            ChannelMember member;
            member.nick = ev.nick;
            member.user = ev.user;
            member.host = ev.host;
            member.account = ev.account;
            state->members[casefold(ev.nick)] = std::move(member);
            ev.target = state->name;
        } else {
            ev.target = channelName;
        }
        emitEvent(ev);
        return;
    }
    if (cmd == "PART") {
        const std::string channelName = msg.param(0);
        IrcEvent ev = eventFrom(msg, IrcEventType::Part);
        ev.text = msg.param(1);
        ChannelState* state = findChannel(channelName);
        ev.target = state ? state->name : channelName;
        emitEvent(ev);
        if (state) {
            if (ev.self) m_channels.erase(casefold(channelName));
            else state->members.erase(casefold(ev.nick));
        }
        return;
    }
    if (cmd == "KICK") {
        IrcEvent ev = eventFrom(msg, IrcEventType::Kick);
        const std::string channelName = msg.param(0);
        ev.arg = msg.param(1);
        ev.text = msg.param(2);
        ChannelState* state = findChannel(channelName);
        ev.target = state ? state->name : channelName;
        const bool kickedSelf = casefold(ev.arg) == casefold(m_nick);
        emitEvent(ev);
        if (state) {
            if (kickedSelf) m_channels.erase(casefold(channelName));
            else state->members.erase(casefold(ev.arg));
        }
        return;
    }
    if (cmd == "QUIT") {
        IrcEvent base = eventFrom(msg, IrcEventType::Quit);
        base.text = msg.param(0);
        const std::string folded = casefold(base.nick);
        for (auto& [_, state] : m_channels) {
            if (state.members.erase(folded) > 0) {
                IrcEvent ev = base;
                ev.target = state.name;
                emitEvent(ev);
            }
        }
        return;
    }
    if (cmd == "NICK") {
        IrcEvent base = eventFrom(msg, IrcEventType::NickChange);
        base.arg = msg.param(0);
        const std::string oldFolded = casefold(base.nick);
        const std::string newFolded = casefold(base.arg);
        if (base.self) m_nick = base.arg;
        for (auto& [_, state] : m_channels) {
            const auto it = state.members.find(oldFolded);
            if (it == state.members.end()) continue;
            ChannelMember member = it->second;
            member.nick = base.arg;
            state.members.erase(it);
            state.members[newFolded] = std::move(member);
            IrcEvent ev = base;
            ev.target = state.name;
            emitEvent(ev);
        }
        if (base.self) {
            IrcEvent ev = base;
            ev.target.clear();
            emitEvent(ev);
        }
        return;
    }
    if (cmd == "MODE") {
        handleMode(msg);
        return;
    }
    if (cmd == "TOPIC") {
        const std::string channelName = msg.param(0);
        IrcEvent ev = eventFrom(msg, IrcEventType::Topic);
        ev.text = msg.param(1);
        ChannelState* state = findChannel(channelName);
        if (state) {
            state->topic = ev.text;
            state->topicSetBy = ev.nick;
        }
        ev.target = state ? state->name : channelName;
        emitEvent(ev);
        return;
    }
    if (cmd == "AWAY" || cmd == "ACCOUNT" || cmd == "CHGHOST" || cmd == "SETNAME") {
        IrcEvent base = eventFrom(msg, IrcEventType::MemberUpdated);
        const std::string folded = casefold(base.nick);
        for (auto& [_, state] : m_channels) {
            const auto it = state.members.find(folded);
            if (it == state.members.end()) continue;
            ChannelMember& member = it->second;
            if (cmd == "AWAY") {
                member.away = !msg.params.empty();
                base.arg = member.away ? "away" : "back";
                base.text = msg.params.empty() ? "" : msg.params.back();
            } else if (cmd == "ACCOUNT") {
                member.account = msg.param(0) == "*" ? "" : msg.param(0);
                base.arg = "account";
            } else if (cmd == "CHGHOST") {
                member.user = msg.param(0);
                member.host = msg.param(1);
                base.arg = "chghost";
            } else {
                base.arg = "setname";
                base.text = msg.param(0);
            }
            IrcEvent ev = base;
            ev.target = state.name;
            emitEvent(ev);
        }
        return;
    }
    if (cmd == "INVITE") {
        IrcEvent ev = eventFrom(msg, IrcEventType::Invite);
        ev.arg = msg.param(1);
        emitEvent(ev);
        return;
    }
    if (cmd == "BATCH") return;  // chathistory batches are consumed line by line for now

    IrcEvent ev = eventFrom(msg, IrcEventType::Unknown);
    for (std::size_t i = 0; i < msg.params.size(); ++i) ev.text += (i > 0 ? " " : "") + msg.params[i];
    emitEvent(ev);
}

void IrcSession::sendRaw(const std::string& line) { m_sink.sendLine(line); }

void IrcSession::sendMessage(const std::string& target, const std::string& text) {
    m_sink.sendLine("PRIVMSG " + target + " :" + text);
    if (!capEnabled("echo-message")) {
        IrcEvent ev;
        ev.type = IrcEventType::Message;
        ev.nick = m_nick;
        ev.target = target;
        ev.text = text;
        ev.self = true;
        emitEvent(ev);
    }
}

void IrcSession::sendAction(const std::string& target, const std::string& text) {
    m_sink.sendLine("PRIVMSG " + target + " :\x01" + "ACTION " + text + "\x01");
    if (!capEnabled("echo-message")) {
        IrcEvent ev;
        ev.type = IrcEventType::Action;
        ev.nick = m_nick;
        ev.target = target;
        ev.text = text;
        ev.self = true;
        emitEvent(ev);
    }
}

void IrcSession::sendNotice(const std::string& target, const std::string& text) {
    m_sink.sendLine("NOTICE " + target + " :" + text);
    if (!capEnabled("echo-message")) {
        IrcEvent ev;
        ev.type = IrcEventType::Notice;
        ev.nick = m_nick;
        ev.target = target;
        ev.text = text;
        ev.self = true;
        emitEvent(ev);
    }
}

void IrcSession::sendCtcpRequest(const std::string& target, const std::string& type, const std::string& args) {
    m_sink.sendLine("PRIVMSG " + target + " :\x01" + type + (args.empty() ? "" : " " + args) + "\x01");
}

void IrcSession::join(const std::string& channel, const std::string& key) {
    m_sink.sendLine("JOIN " + channel + (key.empty() ? "" : " " + key));
}

void IrcSession::part(const std::string& channel, const std::string& reason) {
    m_sink.sendLine("PART " + channel + (reason.empty() ? "" : " :" + reason));
}

void IrcSession::setTopic(const std::string& channel, const std::string& topic) {
    m_sink.sendLine("TOPIC " + channel + " :" + topic);
}

void IrcSession::setAway(const std::string& message) {
    m_sink.sendLine("AWAY :" + (message.empty() ? "Away" : message));
}

void IrcSession::clearAway() { m_sink.sendLine("AWAY"); }

}  // namespace lurker
