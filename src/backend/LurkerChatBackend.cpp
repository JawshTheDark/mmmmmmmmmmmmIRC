// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "backend/LurkerChatBackend.h"

#include <QJsonArray>
#include <algorithm>

namespace lurker::backend {

namespace {
IrcEventType typeFromString(const QString& t) {
    if (t == "message") return IrcEventType::Message;
    if (t == "action") return IrcEventType::Action;
    if (t == "notice") return IrcEventType::Notice;
    if (t == "join") return IrcEventType::Join;
    if (t == "part") return IrcEventType::Part;
    if (t == "quit") return IrcEventType::Quit;
    if (t == "nick") return IrcEventType::NickChange;
    if (t == "kick") return IrcEventType::Kick;
    if (t == "mode") return IrcEventType::Mode;
    if (t == "topic") return IrcEventType::Topic;
    if (t == "invite") return IrcEventType::Invite;
    if (t == "motd") return IrcEventType::MotdLine;
    if (t == "error") return IrcEventType::ErrorText;
    if (t == "system") return IrcEventType::ServerText;
    return IrcEventType::Unknown;
}
}  // namespace

DccTransfer LurkerChatBackend::parseDccTransfer(const QJsonObject& row) {
    DccTransfer t;
    t.id = row.value("id").toInt();
    t.networkId = row.value("network_id").toInt(-1);
    t.peer = row.value("peer_nick").toString();
    t.direction = row.value("direction").toString();
    t.filename = row.value("filename").toString();
    t.size = static_cast<qlonglong>(row.value("advertised_size").toDouble());
    t.received = static_cast<qlonglong>(row.value("received_bytes").toDouble());
    t.state = row.value("state").toString();
    t.error = row.value("error").toString();
    t.fileDeleted = !row.value("file_deleted_at").isNull() && !row.value("file_deleted_at").toString().isEmpty();
    return t;
}

LurkerChatBackend::LurkerChatBackend(QObject* parent) : ChatBackend(parent) {
    connect(&m_client, &LurkerClient::statusText, this, &ChatBackend::statusText);
    connect(&m_client, &LurkerClient::restError, this, [this](const QString& t) { emit statusText(t); });
    connect(&m_client, &LurkerClient::dccChatStarted, this, [this](int net, const QString& target) {
        emit bufferOpened(net, target, static_cast<int>(BufferKind::Dm));
    });
    connect(&m_client, &LurkerClient::dccTransfersList, this, [this](const QJsonArray& arr) {
        QList<DccTransfer> list;
        for (const QJsonValue& v : arr) list.push_back(parseDccTransfer(v.toObject()));
        emit dccTransferList(list);
    });
    connect(&m_client, &LurkerClient::dccTransfer, this,
            [this](const QJsonObject& row) { emit dccTransferUpdated(parseDccTransfer(row)); });
    connect(&m_client, &LurkerClient::dccFileSaved, this,
            [this](int id, const QString& path) { emit dccFileSaved(id, path); });
    connect(&m_client, &LurkerClient::fileUploadProgress, this, &ChatBackend::fileUploadProgress);
    connect(&m_client, &LurkerClient::fileUploaded, this, &ChatBackend::fileUploaded);
    connect(&m_client, &LurkerClient::fileUploadError, this, &ChatBackend::fileUploadError);
    connect(&m_client, &LurkerClient::networksReceived, this, [this](const QJsonArray& nets) {
        for (const QJsonValue& v : nets) {
            const QJsonObject o = v.toObject();
            m_networkNames.insert(o.value("id").toInt(), o.value("name").toString());
        }
    });
    connect(&m_client, &LurkerClient::snapshotReceived, this, &LurkerChatBackend::onSnapshot);
    connect(&m_client, &LurkerClient::backlogReceived, this, &LurkerChatBackend::onBacklog);
    connect(&m_client, &LurkerClient::historyReceived, this, &LurkerChatBackend::onHistory);
    connect(&m_client, &LurkerClient::ircEvent, this, &LurkerChatBackend::onLive);
    // Channel-list responses (chanlist-state echo, chanlist-result pages) arrive
    // as their own frame kinds, not as irc events; progress (chanlist-start/
    // -progress/-end) arrives as kind:'irc' and is caught in translateEvent.
    connect(&m_client, &LurkerClient::frameReceived, this,
            [this](const QString& kind, const QJsonObject& frame) {
                if (kind.startsWith(QStringLiteral("chanlist-"))) handleChanlistFrame(kind, frame);
            });
    connect(&m_client, &LurkerClient::readState, this, [this](const QJsonObject& f) {
        emit readStateChanged(f.value("networkId").toInt(-1), f.value("target").toString(),
                              f.value("unread").toInt(), f.value("highlights").toInt());
    });
    connect(&m_client, &LurkerClient::connected, this, [this] {
        m_ready = true;
        emit readyChanged();
        m_client.fetchDccTransfers();  // seed the Transfers view with existing rows
    });
    connect(&m_client, &LurkerClient::closed, this, [this] {
        m_ready = false;
        emit readyChanged();
        emit closed();
    });
}

void LurkerChatBackend::connectToLurker(const LurkerConnectSettings& settings) { m_client.connectToLurker(settings); }

QString LurkerChatBackend::key(int net, const QString& target) const {
    return QString::number(net) + QLatin1Char('\x1f') + target.toLower();
}

int LurkerChatBackend::guessKind(const QString& target) const {
    if (target.startsWith(QStringLiteral(":server:"))) return static_cast<int>(BufferKind::Server);
    if (!target.isEmpty() && QStringLiteral("#&+!").contains(target.front())) return static_cast<int>(BufferKind::Channel);
    return static_cast<int>(BufferKind::Dm);
}

QChar LurkerChatBackend::sigilFor(int net, const QString& modes) const {
    if (modes.isEmpty()) return QChar();
    const auto it = m_prefix.constFind(net);
    if (it == m_prefix.constEnd()) return QChar();
    return it->sigil.value(modes.front(), QChar());
}

void LurkerChatBackend::emitMembers(int net, const QString& target) {
    const QString k = key(net, target);
    const auto mapIt = m_members.constFind(k);
    if (mapIt == m_members.constEnd()) return;
    const PrefixInfo prefix = m_prefix.value(net);

    QList<Member> list;
    for (const Member& m : *mapIt) list.append(m);
    std::sort(list.begin(), list.end(), [&](const Member& a, const Member& b) {
        const int ra = a.modes.isEmpty() ? 1000 : prefix.order.indexOf(a.modes.front());
        const int rb = b.modes.isEmpty() ? 1000 : prefix.order.indexOf(b.modes.front());
        const int na = ra < 0 ? 1000 : ra;
        const int nb = rb < 0 ? 1000 : rb;
        if (na != nb) return na < nb;
        return a.nick.compare(b.nick, Qt::CaseInsensitive) < 0;
    });

    QList<DisplayMember> out;
    out.reserve(list.size());
    for (const Member& m : list) {
        const QChar s = sigilFor(net, m.modes);
        out.append({s.isNull() ? QString() : QString(s), m.nick, m.away});
    }
    emit membersReplaced(net, target, out);
}

void LurkerChatBackend::replaceMembers(int net, const QString& target, const QJsonArray& members, int) {
    QMap<QString, Member> map;
    for (const QJsonValue& v : members) {
        const QJsonObject o = v.toObject();
        Member m;
        m.nick = o.value("nick").toString();
        for (const QJsonValue& mode : o.value("modes").toArray()) m.modes += mode.toString();
        m.away = o.value("away").toBool();
        if (!m.nick.isEmpty()) map.insert(m.nick.toLower(), m);
    }
    m_members.insert(key(net, target), map);
    emitMembers(net, target);
}

void LurkerChatBackend::onSnapshot(const QJsonObject& frame) {
    for (const QJsonValue& nv : frame.value("networks").toArray()) {
        const QJsonObject net = nv.toObject();
        const int id = net.value("networkId").toInt();

        PrefixInfo prefix;
        for (const QJsonValue& pv : net.value("modeSpec").toObject().value("prefix").toArray()) {
            const QJsonObject p = pv.toObject();
            const QString mode = p.value("mode").toString();
            const QString symbol = p.value("symbol").toString();
            if (!mode.isEmpty()) {
                prefix.order += mode;
                if (!symbol.isEmpty()) prefix.sigil.insert(mode.front(), symbol.front());
            }
        }
        if (prefix.order.isEmpty()) {
            prefix.order = QStringLiteral("qaohv");
            const char* syms = "~&@%+";
            for (int i = 0; i < prefix.order.size(); ++i) prefix.sigil.insert(prefix.order[i], QChar(syms[i]));
        }
        m_prefix.insert(id, prefix);

        NetworkInfo info;
        info.id = id;
        info.name = m_networkNames.value(id, net.value("name").toString());  // names come from REST
        info.state = net.value("state").toString();
        info.nick = net.value("nick").toString();
        emit networkUpdated(info);

        for (const QJsonValue& cv : net.value("channels").toArray()) {
            const QJsonObject chan = cv.toObject();
            const QString name = chan.value("name").toString();
            if (name.isEmpty()) continue;
            emit bufferOpened(id, name, static_cast<int>(BufferKind::Channel));
            emit topicChanged(id, name, chan.value("topic").toString().toUtf8());
            replaceMembers(id, name, chan.value("members").toArray(), id);
        }
    }
}

void LurkerChatBackend::onBacklog(const QJsonObject& frame) {
    const int net = frame.value("networkId").toInt(-1);
    const QString target = frame.value("target").toString();
    if (net < 0 || target.isEmpty()) return;
    emit bufferOpened(net, target, guessKind(target));
    // Seed the unread badge from the snapshot's per-buffer counts.
    if (frame.contains("unread"))
        emit readStateChanged(net, target, frame.value("unread").toInt(), frame.value("highlights").toInt());
    for (const QJsonValue& ev : frame.value("events").toArray()) translateEvent(ev.toObject(), false);
}

void LurkerChatBackend::onHistory(const QJsonObject& frame) {
    for (const QJsonValue& ev : frame.value("events").toArray()) translateEvent(ev.toObject(), false);
}

void LurkerChatBackend::onLive(const QJsonObject& frame) { translateEvent(frame, true); }

void LurkerChatBackend::translateEvent(const QJsonObject& ev, bool /*live*/) {
    const QString typeStr = ev.value("type").toString();
    const IrcEventType type = typeFromString(typeStr);
    const int net = ev.value("networkId").toInt(-1);
    const QString target = ev.value("target").toString();

    // /LIST progress markers (chanlist-start/-progress/-end). The rows live in
    // the server-side cache; when the refresh ends we page it out (below).
    if (typeStr.startsWith(QStringLiteral("chanlist-"))) {
        if (typeStr == QStringLiteral("chanlist-end") && net >= 0 && m_chanlist.contains(net) &&
            !m_chanlist[net].swept)
            beginChanlistSweep(net);
        return;  // never render these as chat lines
    }

    // Keep the per-channel member set live.
    if (typeStr == "names") {
        replaceMembers(net, target, ev.value("members").toArray(), net);
        return;
    }
    if (typeStr == "member-update") {
        const QJsonObject m = ev.value("member").toObject();
        const QString k = key(net, target);
        if (m_members.contains(k)) {
            const QString nick = m.value("nick").toString();
            auto& map = m_members[k];
            auto it = map.find(nick.toLower());
            if (it != map.end()) {
                it->away = m.value("away").toBool(it->away);
                emitMembers(net, target);
            }
        }
        return;
    }

    IrcEvent out;
    out.type = type;
    out.networkId = net;
    out.id = static_cast<long long>(ev.value("id").toDouble());
    out.nick = ev.value("nick").toString().toStdString();
    out.target = target.toStdString();
    out.text = ev.value("text").toString().toStdString();
    out.self = ev.value("self").toBool();
    out.highlight = ev.value("matched").toBool();
    out.time = ev.value("time").toString().toStdString();
    if (ev.contains("msgid")) out.msgid = ev.value("msgid").toString().toStdString();
    if (type == IrcEventType::NickChange) out.arg = ev.value("newNick").toString().toStdString();
    if (type == IrcEventType::Kick) out.arg = ev.value("kicked").toString().toStdString();

    // Incremental membership upkeep for channels.
    if (net >= 0 && !target.isEmpty()) {
        const QString k = key(net, target);
        const QString nick = QString::fromStdString(out.nick);
        auto touch = [&]() -> QMap<QString, Member>& { return m_members[k]; };
        if (type == IrcEventType::Join && m_members.contains(k)) {
            Member m;
            m.nick = nick;
            touch().insert(nick.toLower(), m);
            emitMembers(net, target);
        } else if ((type == IrcEventType::Part || type == IrcEventType::Quit) && m_members.contains(k)) {
            touch().remove(nick.toLower());
            emitMembers(net, target);
        } else if (type == IrcEventType::Kick && m_members.contains(k)) {
            touch().remove(QString::fromStdString(out.arg).toLower());
            emitMembers(net, target);
        } else if (type == IrcEventType::NickChange && m_members.contains(k)) {
            auto& map = touch();
            auto it = map.find(nick.toLower());
            if (it != map.end()) {
                Member m = *it;
                m.nick = QString::fromStdString(out.arg);
                map.erase(it);
                map.insert(m.nick.toLower(), m);
                emitMembers(net, target);
            }
        }
    }

    if (type == IrcEventType::Topic) emit topicChanged(net, target, QString::fromStdString(out.text).toUtf8());
    if (type != IrcEventType::Unknown) emit event(out);
}

// --- verbs ---------------------------------------------------------------

void LurkerChatBackend::sendMessage(int net, const QString& target, const QString& text) {
    m_client.sendChat(net, target, text);
}
void LurkerChatBackend::sendAction(int net, const QString& target, const QString& text) {
    m_client.sendVerb({{"type", "action"}, {"networkId", net}, {"target", target}, {"text", text}});
}
void LurkerChatBackend::sendNotice(int net, const QString& target, const QString& text) {
    m_client.sendVerb({{"type", "notice"}, {"networkId", net}, {"target", target}, {"text", text}});
}
void LurkerChatBackend::sendRaw(int net, const QString& line) {
    m_client.sendVerb({{"type", "raw"}, {"networkId", net}, {"line", line}});
}
void LurkerChatBackend::joinChannel(int net, const QString& channel, const QString& key) {
    QJsonObject v{{"type", "join"}, {"networkId", net}, {"channel", channel}};
    if (!key.isEmpty()) v.insert("key", key);
    m_client.sendVerb(v);
}
void LurkerChatBackend::partChannel(int net, const QString& channel, const QString& reason) {
    QJsonObject v{{"type", "part"}, {"networkId", net}, {"channel", channel}};
    if (!reason.isEmpty()) v.insert("reason", reason);
    m_client.sendVerb(v);
}
void LurkerChatBackend::setTopic(int net, const QString& channel, const QString& topic) {
    m_client.sendVerb({{"type", "set-topic"}, {"networkId", net}, {"channel", channel}, {"topic", topic}});
}
void LurkerChatBackend::setAway(int net, const QString& message) {
    m_client.sendVerb({{"type", "away"}, {"networkId", net}, {"message", message}});
}
void LurkerChatBackend::clearAway(int net) { m_client.sendVerb({{"type", "back"}, {"networkId", net}}); }
void LurkerChatBackend::sendCtcp(int net, const QString& target, const QString& type, const QString& args) {
    m_client.sendVerb(
        {{"type", "ctcp"}, {"networkId", net}, {"target", target}, {"ctcpType", type}, {"args", args}});
}
void LurkerChatBackend::requestHistory(int net, const QString& target) {
    m_client.requestHistoryLatest(net, target, 50);
}
void LurkerChatBackend::markRead(int net, const QString& target, qlonglong messageId) {
    m_client.markRead(net, target, messageId);
}
void LurkerChatBackend::disconnectAll() { m_client.disconnect(); }

// --- channel list --------------------------------------------------------

void LurkerChatBackend::requestChannelList(int net) {
    if (net < 0) return;
    m_chanlist.insert(net, ChanlistReq{});  // fresh request; wait for the refresh to finish
    // Kick a server-side /LIST. The server clears its cache, relays /LIST, and
    // reports progress; we sweep the cache on chanlist-end (or on a warm
    // chanlist-state echo, if the cache is already populated and idle).
    m_client.sendVerb({{"type", "list-channels"}, {"networkId", net}});
}

void LurkerChatBackend::handleChanlistFrame(const QString& kind, const QJsonObject& frame) {
    const int net = frame.value("networkId").toInt(-1);
    if (net < 0 || !m_chanlist.contains(net)) return;  // not a sweep we started

    if (kind == QStringLiteral("chanlist-state")) {
        // Echoed when the refresh is requested. If the cache is already warm and
        // idle, page it out now rather than waiting for a chanlist-end that may
        // not come (e.g. the refresh was dropped as a duplicate).
        const bool inProgress = frame.value("inProgress").toBool();
        const int totalCount = frame.value("totalCount").toInt();
        if (!inProgress && totalCount > 0 && !m_chanlist[net].swept) beginChanlistSweep(net);
        return;
    }
    if (kind == QStringLiteral("chanlist-result")) {
        const QJsonArray rows = frame.value("rows").toArray();
        const int total = frame.value("total").toInt();
        const int offset = frame.value("offset").toInt();
        for (const QJsonValue& v : rows) {
            const QJsonObject row = v.toObject();
            const QString chan = row.value("channel").toString();
            if (chan.isEmpty()) continue;
            const int users = row.value("num_users").toInt();
            const QString topic = row.value("topic").toString();
            // 322: "<channel> <users> <topic>" — the GUI's RPL_LIST router parses
            // this back into (channel, users, topic) for the Channels List window.
            emitListNumeric(net, 322, chan + QLatin1Char(' ') + QString::number(users) +
                                          QLatin1Char(' ') + topic);
        }
        const int got = offset + rows.size();
        m_chanlist[net].offset = got;
        m_chanlist[net].total = total;
        if (got < total && !rows.isEmpty()) {
            sendChanlistSearch(net, got);  // next page
        } else {
            emitListNumeric(net, 323, QStringLiteral("End of /LIST"));
            m_chanlist.remove(net);
        }
        return;
    }
}

void LurkerChatBackend::beginChanlistSweep(int net) {
    m_chanlist[net].swept = true;
    m_chanlist[net].offset = 0;
    sendChanlistSearch(net, 0);
}

void LurkerChatBackend::sendChanlistSearch(int net, int offset) {
    m_client.sendVerb({{"type", "chanlist-search"},
                       {"networkId", net},
                       {"query", QString()},
                       {"sortBy", "users"},
                       {"sortDir", "desc"},
                       {"offset", offset},
                       {"limit", 500}});
}

void LurkerChatBackend::emitListNumeric(int net, int numeric, const QString& text) {
    IrcEvent out;
    out.type = IrcEventType::ServerText;
    out.networkId = net;
    out.numeric = numeric;
    out.text = text.toStdString();
    emit event(out);
}

}  // namespace lurker::backend
