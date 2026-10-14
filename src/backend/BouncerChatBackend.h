// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// BouncerChatBackend adapts the raw-IRC BouncerBackend to the ChatBackend
// interface so the GUI renders it the same way as the Lurker path. It is a
// single-network backend (fixed id 1); membership and topics are read from the
// underlying IrcSession and re-emitted as ChatBackend signals.
#pragma once

#include "backend/BouncerBackend.h"
#include "backend/ChatBackend.h"

namespace lurker::backend {

class BouncerChatBackend : public ChatBackend {
    Q_OBJECT
public:
    explicit BouncerChatBackend(QObject* parent = nullptr);

    void connectToHost(const ConnectSettings& settings);

    bool isReady() const override { return m_bouncer.isRegistered(); }
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

    static constexpr int kNetworkId = 1;

private:
    void onEvent(const lurker::IrcEvent& incoming);
    void emitMembers(const QString& channel);

    BouncerBackend m_bouncer;
};

}  // namespace lurker::backend
