// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "backend/BouncerChatBackend.h"

#include <algorithm>
#include <climits>

namespace lurker::backend {

BouncerChatBackend::BouncerChatBackend(QObject* parent) : ChatBackend(parent) {
    connect(&m_bouncer, &BouncerBackend::statusText, this, &ChatBackend::statusText);
    connect(&m_bouncer, &BouncerBackend::eventReceived, this, &BouncerChatBackend::onEvent);
    connect(&m_bouncer, &BouncerBackend::connectedAndRegistered, this, [this] {
        NetworkInfo info;
        info.id = kNetworkId;
        info.name = QString::fromStdString(m_bouncer.session()->isupport().network);
        if (info.name.isEmpty()) info.name = tr("IRC");
        info.state = QStringLiteral("connected");
        info.nick = QString::fromStdString(m_bouncer.session()->nick());
        emit networkUpdated(info);
        emit readyChanged();
    });
    connect(&m_bouncer, &BouncerBackend::disconnected, this, [this] {
        emit readyChanged();
        emit closed();
    });
}

void BouncerChatBackend::connectToHost(const ConnectSettings& settings) { m_bouncer.connectToHost(settings); }

void BouncerChatBackend::emitMembers(const QString& channel) {
    lurker::IrcSession* s = m_bouncer.session();
    if (!s) return;
    const lurker::ChannelState* chan = s->channel(channel.toStdString());
    if (!chan) return;
    const lurker::Isupport& isupport = s->isupport();

    std::vector<const lurker::ChannelMember*> members;
    members.reserve(chan->members.size());
    for (const auto& [_, m] : chan->members) members.push_back(&m);
    std::sort(members.begin(), members.end(), [&](const lurker::ChannelMember* a, const lurker::ChannelMember* b) {
        const int ra = a->prefixModes.empty() ? INT_MAX : isupport.rankOfMode(a->prefixModes.front());
        const int rb = b->prefixModes.empty() ? INT_MAX : isupport.rankOfMode(b->prefixModes.front());
        if (ra != rb) return ra < rb;
        return QString::fromStdString(a->nick).compare(QString::fromStdString(b->nick), Qt::CaseInsensitive) < 0;
    });

    QList<DisplayMember> out;
    out.reserve(static_cast<int>(members.size()));
    for (const lurker::ChannelMember* m : members) {
        const char sigil = m->prefixModes.empty() ? 0 : isupport.sigilForMode(m->prefixModes.front());
        out.append({sigil ? QString(QChar(sigil)) : QString(), QString::fromStdString(m->nick), m->away});
    }
    emit membersReplaced(kNetworkId, channel, out);
}

void BouncerChatBackend::onEvent(const lurker::IrcEvent& incoming) {
    lurker::IrcEvent ev = incoming;
    ev.networkId = kNetworkId;
    const QString target = QString::fromStdString(ev.target);

    if (ev.type == IrcEventType::Join && ev.self) {
        emit bufferOpened(kNetworkId, target, static_cast<int>(BufferKind::Channel));
    }
    if (ev.type == IrcEventType::Topic || ev.type == IrcEventType::TopicState) {
        emit topicChanged(kNetworkId, target, QByteArray::fromStdString(ev.text));
    }

    emit event(ev);

    switch (ev.type) {
        case IrcEventType::Join:
        case IrcEventType::Part:
        case IrcEventType::Quit:
        case IrcEventType::Kick:
        case IrcEventType::NickChange:
        case IrcEventType::Mode:
        case IrcEventType::NamesUpdated:
        case IrcEventType::MemberUpdated:
            if (!target.isEmpty()) emitMembers(target);
            break;
        default:
            break;
    }
}

void BouncerChatBackend::sendMessage(int, const QString& target, const QString& text) {
    if (auto* s = m_bouncer.session()) s->sendMessage(target.toStdString(), text.toStdString());
}
void BouncerChatBackend::sendAction(int, const QString& target, const QString& text) {
    if (auto* s = m_bouncer.session()) s->sendAction(target.toStdString(), text.toStdString());
}
void BouncerChatBackend::sendNotice(int, const QString& target, const QString& text) {
    if (auto* s = m_bouncer.session()) s->sendNotice(target.toStdString(), text.toStdString());
}
void BouncerChatBackend::sendRaw(int, const QString& line) {
    if (auto* s = m_bouncer.session()) s->sendRaw(line.toStdString());
}
void BouncerChatBackend::joinChannel(int, const QString& channel, const QString& key) {
    if (auto* s = m_bouncer.session()) s->join(channel.toStdString(), key.toStdString());
}
void BouncerChatBackend::partChannel(int, const QString& channel, const QString& reason) {
    if (auto* s = m_bouncer.session()) s->part(channel.toStdString(), reason.toStdString());
}
void BouncerChatBackend::setTopic(int, const QString& channel, const QString& topic) {
    if (auto* s = m_bouncer.session()) s->setTopic(channel.toStdString(), topic.toStdString());
}
void BouncerChatBackend::setAway(int, const QString& message) {
    if (auto* s = m_bouncer.session()) s->setAway(message.toStdString());
}
void BouncerChatBackend::clearAway(int) {
    if (auto* s = m_bouncer.session()) s->clearAway();
}
void BouncerChatBackend::sendCtcp(int, const QString& target, const QString& type, const QString& args) {
    if (auto* s = m_bouncer.session()) s->sendCtcpRequest(target.toStdString(), type.toStdString(), args.toStdString());
}
void BouncerChatBackend::requestHistory(int, const QString&) {
    // The Lurker bouncer replays recent history on attach; scroll-up CHATHISTORY
    // is a later enhancement (needs the draft/chathistory cap negotiated).
}
void BouncerChatBackend::disconnectAll() { m_bouncer.disconnectFromHost(); }

}  // namespace lurker::backend
