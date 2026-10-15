// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// LurkerChatBackend adapts LurkerClient (Lurker WS/REST frames) to the
// backend-agnostic ChatBackend the GUI drives. It translates the snapshot
// burst, backlog, history and live irc frames into bufferOpened / event /
// membersReplaced / topicChanged signals, and tracks per-channel membership so
// nicklists stay correct as people join, part, quit and rename.
#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMap>

#include "backend/ChatBackend.h"
#include "backend/LurkerClient.h"

namespace lurker::backend {

class LurkerChatBackend : public ChatBackend {
    Q_OBJECT
public:
    explicit LurkerChatBackend(QObject* parent = nullptr);

    void connectToLurker(const LurkerConnectSettings& settings);

    bool isReady() const override { return m_ready; }
    void sendMessage(int net, const QString& target, const QString& text) override;
    void sendAction(int net, const QString& target, const QString& text) override;
    void sendNotice(int net, const QString& target, const QString& text) override;
    void sendRaw(int net, const QString& line) override;
    void joinChannel(int net, const QString& channel, const QString& key) override;
    void partChannel(int net, const QString& channel, const QString& reason) override;
    void setTopic(int net, const QString& channel, const QString& topic) override;
    void setAway(int net, const QString& message) override;
    void clearAway(int net) override;
    void sendCtcp(int net, const QString& target, const QString& type, const QString& args) override;
    void requestHistory(int net, const QString& target) override;
    void markRead(int net, const QString& target, qlonglong messageId) override;
    void disconnectAll() override;
    bool supportsDccChat() const override { return true; }
    void startDccChat(int net, const QString& nick) override { m_client.startDccChat(net, nick); }
    bool supportsPreviews() const override { return m_client.hasFeature(QStringLiteral("linkPreviews")); }
    QString previewBase() const override { return m_client.restBase(); }
    QString authToken() const override { return m_client.token(); }
    bool supportsDccTransfers() const override { return true; }
    void requestDccTransfers() override { m_client.fetchDccTransfers(); }
    void acceptDcc(int id) override { m_client.dccAccept(id); }
    void rejectDcc(int id) override { m_client.dccReject(id); }
    void cancelDcc(int id) override { m_client.dccCancel(id); }
    void deleteDccFile(int id) override { m_client.deleteDccFile(id); }
    void saveDccFile(int id, const QString& path) override { m_client.saveDccFile(id, path); }
    bool supportsFileSend() const override { return true; }
    void sendFile(int net, const QString& target, const QString& localPath) override {
        m_client.sendFile(net, target, localPath);
    }

private:
    struct Member {
        QString nick;
        QString modes;  // prefix-mode letters, highest rank first
        bool away = false;
    };
    struct PrefixInfo {
        QString order;                 // mode letters high→low, e.g. "qaohv"
        QHash<QChar, QChar> sigil;     // mode letter -> sigil
    };

    void onSnapshot(const QJsonObject& frame);
    void onBacklog(const QJsonObject& frame);
    void onHistory(const QJsonObject& frame);
    void onLive(const QJsonObject& frame);
    void translateEvent(const QJsonObject& ev, bool live);

    static DccTransfer parseDccTransfer(const QJsonObject& row);  // JSON row → UI struct
    QString key(int net, const QString& target) const;
    int guessKind(const QString& target) const;  // BufferKind as int
    void emitMembers(int net, const QString& target);
    void replaceMembers(int net, const QString& target, const QJsonArray& members, int networkForPrefix);
    QChar sigilFor(int net, const QString& modes) const;

    LurkerClient m_client;
    bool m_ready = false;
    QHash<int, QString> m_networkNames;                  // networkId -> display name (from REST)
    QHash<int, PrefixInfo> m_prefix;                     // networkId -> prefix info
    QHash<QString, QMap<QString, Member>> m_members;     // buffer key -> folded nick -> member
};

}  // namespace lurker::backend
