// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// LurkerClient speaks Lurker's own client protocol: the REST surface under
// /api/* and the single WebSocket at /ws. Unlike the bouncer (raw IRC), this
// path carries server-digested JSON frames and the Lurker-only features —
// stored history, search, read state, uploads, reactions. Written from
// docs/CLIENT_PROTOCOL.md. Auth is a Bearer token: either minted from a
// username+password login, or supplied directly.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QWebSocket;

namespace lurker::backend {

struct LurkerConnectSettings {
    QString host;            // e.g. chat.irc.so
    bool tls = true;         // https/wss vs http/ws
    QString token;           // Bearer token (minted session token or OAuth access token)
    QString username;        // used only when token is empty, to mint via login
    QString password;        // "
    bool ignoreTlsErrors = false;
    QString pinnedSha256;    // accept exactly this certificate
};

// The server's advertised configuration (GET /api/config).
struct LurkerConfig {
    QString edition;         // "standalone" | "node"
    int protocolVersion = 0;
    int minProtocolVersion = 0;
    QJsonObject features;    // instance feature flags (linkPreviews, bouncer, ...)
    bool valid = false;
};

class LurkerClient : public QObject {
    Q_OBJECT
public:
    explicit LurkerClient(QObject* parent = nullptr);
    ~LurkerClient() override;

    // Fetches /api/config (no auth). Emits configReceived or restError.
    void fetchConfig(const QString& host, bool tls);

    // Begins the full connect: mint a token if needed, GET /api/networks, open
    // the WebSocket, and stream frames. Emits connected on the first WS frame.
    void connectToLurker(const LurkerConnectSettings& settings);
    void disconnect();

    qint64 resumeCursor() const { return m_cursor; }

    // Client -> server verbs (sent as JSON over the WS).
    void sendChat(int networkId, const QString& target, const QString& text, const QString& clientId = {});
    void requestHistoryLatest(int networkId, const QString& target, int limit = 50, const QString& token = {});
    void markRead(int networkId, const QString& target, qint64 messageId);
    void sendVerb(const QJsonObject& message);

signals:
    void configReceived(const lurker::backend::LurkerConfig& config);
    void networksReceived(const QJsonArray& networks);
    void connected();                                  // first WS frame arrived
    void snapshotReceived(const QJsonObject& frame);   // kind:snapshot
    void backlogReceived(const QJsonObject& frame);    // kind:backlog (per buffer)
    void backlogComplete();                            // kind:backlog-complete
    void historyReceived(const QJsonObject& frame);    // kind:history
    void ircEvent(const QJsonObject& frame);           // kind:irc (live MessageEvent)
    void readState(const QJsonObject& frame);          // kind:read-state
    void sendResult(const QJsonObject& frame);         // kind:send-result
    void frameReceived(const QString& kind, const QJsonObject& frame);  // every frame
    void statusText(const QString& text);
    void restError(const QString& text);
    void closed();

private:
    QString baseUrl(const QString& host, bool tls) const;  // https://host or http://host
    void mintTokenThenContinue();
    void fetchNetworksThenConnect();
    void openWebSocket();
    void onTextFrame(const QString& text);
    void applyTlsConfig();

    QNetworkAccessManager* m_net = nullptr;
    QWebSocket* m_ws = nullptr;
    LurkerConnectSettings m_settings;
    qint64 m_cursor = 0;      // highest persisted message id seen (resume cursor)
    bool m_firstFrame = true;
    quint64 m_clientIdSeq = 0;
};

}  // namespace lurker::backend
