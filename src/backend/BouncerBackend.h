// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// BouncerBackend owns the TLS socket to an IRC server (for this project,
// Lurker's built-in bouncer) and drives an IrcSession over it: line
// reassembly, reconnect with backoff, rejoin after reconnect, and Qt signals
// for the session's events.
#pragma once

#include <QObject>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <memory>

#include "core/IrcSession.h"

namespace lurker::backend {

struct ConnectSettings {
    QString host;
    quint16 port = 6697;
    bool tls = true;
    bool ignoreTlsErrors = false;  // last resort; logged loudly
    QString pinnedSha256;          // accept exactly this certificate (hex, colons optional)
    QString nick;
    QString username;  // Lurker: "user" or "user/network"
    QString password;  // SASL PLAIN secret (Lurker: account password or API token)
    QString realname;
};

class BouncerBackend : public QObject, private lurker::IrcSessionSink {
    Q_OBJECT
public:
    explicit BouncerBackend(QObject* parent = nullptr);
    ~BouncerBackend() override;

    void connectToHost(const ConnectSettings& settings);
    void disconnectFromHost();

    bool isActive() const { return m_socket != nullptr; }
    bool isRegistered() const { return m_session && m_session->registered(); }
    lurker::IrcSession* session() { return m_session.get(); }
    QString casefold(const QString& s) const;

signals:
    void eventReceived(const lurker::IrcEvent& event);
    void statusText(const QString& text);  // connection lifecycle lines for the status window
    void connectedAndRegistered();
    void disconnected();

private:
    // IrcSessionSink
    void sendLine(const std::string& line) override;
    void onEvent(const lurker::IrcEvent& event) override;

    void openSocket();
    void handleReadyRead();
    void handleDisconnected();
    void scheduleReconnect();

    ConnectSettings m_settings;
    std::unique_ptr<QSslSocket> m_socket;
    std::unique_ptr<lurker::IrcSession> m_session;
    QByteArray m_buffer;
    QTimer m_reconnectTimer;
    int m_reconnectDelaySeconds = 2;
    bool m_wantReconnect = false;   // false during manual disconnect / fatal auth failure
    bool m_everRegistered = false;  // only reconnect automatically after one success
    QStringList m_rejoinChannels;   // channels held at the moment the link dropped
};

}  // namespace lurker::backend
