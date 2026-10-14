// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ChatBackend is the backend-agnostic interface the GUI drives. Two adapters
// implement it: BouncerChatBackend (raw IRC, one network) and LurkerChatBackend
// (Lurker WS/REST, every network at once). The GUI knows only this interface,
// so one set of windows renders both. Buffer creation, messages, membership and
// topics all arrive as the signals below, each tagged with a networkId.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "core/IrcEvent.h"

namespace lurker::backend {

struct NetworkInfo {
    int id = -1;
    QString name;   // display name ("Libera.Chat")
    QString state;  // "connecting" | "connected" | "disconnected" | ...
    QString nick;   // our nick on this network
};

// A nicklist entry ready to show: the adapter has already resolved the rank
// sigil from the network's PREFIX/modeSpec and sorted the list.
struct DisplayMember {
    QString sigil;  // "@", "+", "" ...
    QString nick;
    bool away = false;
};

enum class BufferKind { Channel = 0, Dm = 1, Server = 2 };

class ChatBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ChatBackend() override = default;

    virtual bool isReady() const = 0;  // registered / connected enough to send

    // Client -> server. `net` selects the network; adapters that hold a single
    // network ignore it.
    virtual void sendMessage(int net, const QString& target, const QString& text) = 0;
    virtual void sendAction(int net, const QString& target, const QString& text) = 0;
    virtual void sendNotice(int net, const QString& target, const QString& text) = 0;
    virtual void sendRaw(int net, const QString& line) = 0;
    virtual void joinChannel(int net, const QString& channel, const QString& key) = 0;
    virtual void partChannel(int net, const QString& channel, const QString& reason) = 0;
    virtual void setTopic(int net, const QString& channel, const QString& topic) = 0;
    virtual void setAway(int net, const QString& message) = 0;
    virtual void clearAway(int net) = 0;
    virtual void sendCtcp(int net, const QString& target, const QString& type, const QString& args) = 0;
    virtual void requestHistory(int net, const QString& target) = 0;
    virtual void disconnectAll() = 0;

signals:
    void statusText(const QString& text);
    void networkUpdated(const lurker::backend::NetworkInfo& net);
    void bufferOpened(int net, const QString& target, int kind);  // kind = BufferKind
    void event(const lurker::IrcEvent& ev);                        // one line to append
    void membersReplaced(int net, const QString& target, const QList<lurker::backend::DisplayMember>& members);
    void topicChanged(int net, const QString& target, const QByteArray& topic);
    void readyChanged();
    void closed();
};

}  // namespace lurker::backend
