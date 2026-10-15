// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "backend/LurkerClient.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QUrl>
#include <QWebSocket>

namespace lurker::backend {

namespace {
QString normalizeHex(QString s) { return s.remove(QLatin1Char(':')).toLower(); }
}  // namespace

LurkerClient::LurkerClient(QObject* parent) : QObject(parent) {
    m_net = new QNetworkAccessManager(this);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (!m_userClosed) openWebSocket();  // resumes from m_cursor via ?since
    });
}

LurkerClient::~LurkerClient() { disconnect(); }

QString LurkerClient::baseUrl(const QString& host, bool tls) const {
    return (tls ? QStringLiteral("https://") : QStringLiteral("http://")) + host;
}

void LurkerClient::fetchConfig(const QString& host, bool tls) {
    QNetworkRequest req{QUrl(baseUrl(host, tls) + QStringLiteral("/api/config"))};
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit restError(tr("config: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        LurkerConfig c;
        c.edition = o.value(QStringLiteral("edition")).toString();
        c.protocolVersion = o.value(QStringLiteral("protocolVersion")).toInt();
        c.minProtocolVersion = o.value(QStringLiteral("minProtocolVersion")).toInt();
        c.features = o.value(QStringLiteral("features")).toObject();
        c.valid = !c.edition.isEmpty();
        m_config = c;  // remember feature flags (e.g. linkPreviews) for callers
        emit configReceived(c);
    });
}

void LurkerClient::connectToLurker(const LurkerConnectSettings& settings) {
    m_settings = settings;
    m_userClosed = false;
    m_reconnectDelay = 2;
    fetchConfig(settings.host, settings.tls);  // populate feature flags (linkPreviews, …)
    if (m_settings.token.isEmpty()) mintTokenThenContinue();
    else fetchNetworksThenConnect();
}

void LurkerClient::mintTokenThenContinue() {
    QNetworkRequest req{QUrl(baseUrl(m_settings.host, m_settings.tls) + QStringLiteral("/api/auth/login/token"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body{{QStringLiteral("username"), m_settings.username}, {QStringLiteral("password"), m_settings.password}};
    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() != QNetworkReply::NoError || !o.contains(QStringLiteral("token"))) {
            emit restError(tr("login: %1").arg(o.value(QStringLiteral("error")).toString(reply->errorString())));
            return;
        }
        m_settings.token = o.value(QStringLiteral("token")).toString();
        emit statusText(tr("Signed in; connecting..."));
        fetchNetworksThenConnect();
    });
}

void LurkerClient::fetchNetworksThenConnect() {
    QNetworkRequest req{QUrl(baseUrl(m_settings.host, m_settings.tls) + QStringLiteral("/api/networks"))};
    req.setRawHeader("Authorization", "Bearer " + m_settings.token.toUtf8());
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit restError(tr("networks: %1").arg(reply->errorString()));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        emit networksReceived(o.value(QStringLiteral("networks")).toArray());
        openWebSocket();
    });
}

void LurkerClient::startDccChat(int networkId, const QString& nick) {
    QNetworkRequest req{QUrl(baseUrl(m_settings.host, m_settings.tls) + QStringLiteral("/api/dcc/chat"))};
    req.setRawHeader("Authorization", "Bearer " + m_settings.token.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QJsonObject body{{QStringLiteral("networkId"), networkId}, {QStringLiteral("nick"), nick}};
    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, networkId, nick] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit restError(tr("DCC chat with %1: %2").arg(nick, reply->errorString()));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString target = o.value(QStringLiteral("target")).toString(QStringLiteral("=") + nick);
        emit statusText(tr("DCC chat offered to %1").arg(nick));
        emit dccChatStarted(networkId, target);
    });
}

void LurkerClient::applyTlsConfig() {
    if (!m_settings.tls) return;
    connect(m_ws, &QWebSocket::sslErrors, this, [this](const QList<QSslError>& errors) {
        const QSslCertificate peer = m_ws->sslConfiguration().peerCertificate();
        const QString digest = QString::fromLatin1(peer.digest(QCryptographicHash::Sha256).toHex(':'));
        for (const QSslError& e : errors) emit statusText(tr("TLS: %1 (%2)").arg(e.errorString(), digest));
        if (!m_settings.pinnedSha256.isEmpty() && !peer.isNull() &&
            normalizeHex(m_settings.pinnedSha256) == normalizeHex(digest)) {
            m_ws->ignoreSslErrors();
        } else if (m_settings.ignoreTlsErrors) {
            m_ws->ignoreSslErrors();
        }
    });
}

void LurkerClient::openWebSocket() {
    const QString scheme = m_settings.tls ? QStringLiteral("wss://") : QStringLiteral("ws://");
    QUrl url(scheme + m_settings.host + QStringLiteral("/ws"));
    QString query = QStringLiteral("v=1");
    if (m_cursor > 0) query += QStringLiteral("&since=%1").arg(m_cursor);
    url.setQuery(query);

    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->deleteLater();
    }
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    applyTlsConfig();
    connect(m_ws, &QWebSocket::textMessageReceived, this, &LurkerClient::onTextFrame);
    connect(m_ws, &QWebSocket::disconnected, this, &LurkerClient::handleWsClosed);
    connect(m_ws, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { emit statusText(tr("WS error: %1").arg(m_ws->errorString())); });

    QNetworkRequest req{url};
    req.setRawHeader("Authorization", "Bearer " + m_settings.token.toUtf8());
    m_firstFrame = true;
    emit statusText(tr("Opening WebSocket..."));
    m_ws->open(req);
}

void LurkerClient::handleWsClosed() {
    emit closed();
    if (m_userClosed) return;
    emit statusText(tr("Connection lost. Reconnecting in %1 s...").arg(m_reconnectDelay));
    m_reconnectTimer.start(m_reconnectDelay * 1000);
    m_reconnectDelay = qMin(m_reconnectDelay * 2, 30);
}

void LurkerClient::onTextFrame(const QString& text) {
    if (m_firstFrame) {
        m_firstFrame = false;
        m_reconnectDelay = 2;  // a good connection resets the backoff
        emit connected();
    }
    const QJsonObject frame = QJsonDocument::fromJson(text.toUtf8()).object();
    const QString kind = frame.value(QStringLiteral("kind")).toString();
    emit frameReceived(kind, frame);

    // Advance the resume cursor from any persisted event carrying an id on a
    // real network (never the system buffer: networkId null has its own ids).
    auto advance = [this](const QJsonObject& ev) {
        if (ev.value(QStringLiteral("networkId")).isNull()) return;
        const qint64 id = static_cast<qint64>(ev.value(QStringLiteral("id")).toDouble());
        if (id > m_cursor) m_cursor = id;
    };

    if (kind == QStringLiteral("snapshot")) {
        const QJsonValue cursor = frame.value(QStringLiteral("cursor"));
        if (cursor.isDouble()) m_cursor = static_cast<qint64>(cursor.toDouble());
        emit snapshotReceived(frame);
    } else if (kind == QStringLiteral("backlog")) {
        for (const QJsonValue& ev : frame.value(QStringLiteral("events")).toArray()) advance(ev.toObject());
        emit backlogReceived(frame);
    } else if (kind == QStringLiteral("backlog-complete")) {
        emit backlogComplete();
    } else if (kind == QStringLiteral("history")) {
        for (const QJsonValue& ev : frame.value(QStringLiteral("events")).toArray()) advance(ev.toObject());
        emit historyReceived(frame);
    } else if (kind == QStringLiteral("irc")) {
        advance(frame);
        emit ircEvent(frame);
    } else if (kind == QStringLiteral("read-state")) {
        emit readState(frame);
    } else if (kind == QStringLiteral("send-result")) {
        emit sendResult(frame);
    } else if (kind == QStringLiteral("error")) {
        emit restError(frame.value(QStringLiteral("text")).toString());
    }
}

void LurkerClient::sendVerb(const QJsonObject& message) {
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        m_ws->sendTextMessage(QString::fromUtf8(QJsonDocument(message).toJson(QJsonDocument::Compact)));
    }
}

void LurkerClient::sendChat(int networkId, const QString& target, const QString& text, const QString& clientId) {
    QString cid = clientId;
    if (cid.isEmpty()) cid = QStringLiteral("c%1").arg(++m_clientIdSeq);
    sendVerb({{QStringLiteral("type"), QStringLiteral("send")},
              {QStringLiteral("networkId"), networkId},
              {QStringLiteral("target"), target},
              {QStringLiteral("text"), text},
              {QStringLiteral("clientId"), cid}});
}

void LurkerClient::requestHistoryLatest(int networkId, const QString& target, int limit, const QString& token) {
    QJsonObject v{{QStringLiteral("type"), QStringLiteral("history")},
                  {QStringLiteral("networkId"), networkId},
                  {QStringLiteral("target"), target},
                  {QStringLiteral("mode"), QStringLiteral("latest")},
                  {QStringLiteral("limit"), limit}};
    if (!token.isEmpty()) v.insert(QStringLiteral("token"), token);
    sendVerb(v);
}

void LurkerClient::markRead(int networkId, const QString& target, qint64 messageId) {
    sendVerb({{QStringLiteral("type"), QStringLiteral("mark-read")},
              {QStringLiteral("networkId"), networkId},
              {QStringLiteral("target"), target},
              {QStringLiteral("messageId"), static_cast<double>(messageId)}});
}

void LurkerClient::disconnect() {
    m_userClosed = true;
    m_reconnectTimer.stop();
    if (m_ws) {
        m_ws->disconnect(this);
        m_ws->close();
        m_ws->deleteLater();
        m_ws = nullptr;
    }
}

}  // namespace lurker::backend
