// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "backend/BouncerBackend.h"

#include <QSslCertificate>

namespace lurker::backend {

BouncerBackend::BouncerBackend(QObject* parent) : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_wantReconnect) openSocket();
    });
}

BouncerBackend::~BouncerBackend() { disconnectFromHost(); }

QString BouncerBackend::casefold(const QString& s) const {
    if (m_session) return QString::fromStdString(m_session->casefold(s.toStdString()));
    return s.toLower();
}

void BouncerBackend::connectToHost(const ConnectSettings& settings) {
    disconnectFromHost();
    m_settings = settings;
    m_wantReconnect = true;
    m_everRegistered = false;
    m_reconnectDelaySeconds = 2;
    m_rejoinChannels.clear();
    openSocket();
}

void BouncerBackend::openSocket() {
    SessionConfig config;
    config.nick = m_settings.nick.toStdString();
    config.username = m_settings.username.isEmpty() ? m_settings.nick.toStdString()
                                                    : m_settings.username.toStdString();
    config.password = m_settings.password.toStdString();
    if (!m_settings.realname.isEmpty()) config.realname = m_settings.realname.toStdString();
    m_session = std::make_unique<IrcSession>(std::move(config), static_cast<lurker::IrcSessionSink&>(*this));
    m_buffer.clear();

    m_socket = std::make_unique<QSslSocket>(this);
    connect(m_socket.get(), &QSslSocket::readyRead, this, &BouncerBackend::handleReadyRead);
    connect(m_socket.get(), &QSslSocket::disconnected, this, &BouncerBackend::handleDisconnected);
    connect(m_socket.get(), &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket) emit statusText(tr("Connection error: %1").arg(m_socket->errorString()));
    });
    if (m_settings.tls) {
        connect(m_socket.get(), &QSslSocket::sslErrors, this, [this](const QList<QSslError>& errors) {
            const QSslCertificate peer = m_socket->peerCertificate();
            const QString digest = QString::fromLatin1(peer.digest(QCryptographicHash::Sha256).toHex(':'));
            for (const QSslError& e : errors) {
                emit statusText(tr("TLS: %1 (certificate SHA-256 %2)").arg(e.errorString(), digest));
            }
            const auto normalize = [](QString s) { return s.remove(QLatin1Char(':')).toLower(); };
            if (!m_settings.pinnedSha256.isEmpty() && !peer.isNull() &&
                normalize(m_settings.pinnedSha256) == normalize(digest)) {
                emit statusText(tr("TLS: certificate matches the pinned fingerprint, continuing"));
                m_socket->ignoreSslErrors();
            } else if (m_settings.ignoreTlsErrors) {
                emit statusText(tr("TLS: continuing anyway — certificate verification is OFF for this connection"));
                m_socket->ignoreSslErrors();
            }
        });
        connect(m_socket.get(), &QSslSocket::encrypted, this, [this] {
            emit statusText(tr("TLS established, signing in..."));
            m_session->start();
        });
        emit statusText(tr("Connecting to %1:%2 (TLS)...").arg(m_settings.host).arg(m_settings.port));
        m_socket->connectToHostEncrypted(m_settings.host, m_settings.port);
    } else {
        connect(m_socket.get(), &QSslSocket::connected, this, [this] {
            emit statusText(tr("Connected, signing in..."));
            m_session->start();
        });
        emit statusText(tr("Connecting to %1:%2...").arg(m_settings.host).arg(m_settings.port));
        m_socket->connectToHost(m_settings.host, m_settings.port);
    }
}

void BouncerBackend::disconnectFromHost() {
    m_wantReconnect = false;
    m_reconnectTimer.stop();
    if (m_socket) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            m_socket->write("QUIT :Lurker Desktop closing\r\n");
            m_socket->flush();
        }
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket.reset();
        m_session.reset();
        emit disconnected();
        emit statusText(tr("Disconnected."));
    }
}

void BouncerBackend::handleReadyRead() {
    m_buffer += m_socket->readAll();
    int nl = -1;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty() && m_session) {
            m_session->feedLine(std::string_view(line.constData(), static_cast<std::size_t>(line.size())));
        }
    }
}

void BouncerBackend::handleDisconnected() {
    if (m_session) {
        m_rejoinChannels.clear();
        for (const std::string& name : m_session->channelNames()) {
            m_rejoinChannels << QString::fromStdString(name);
        }
    }
    m_session.reset();
    m_socket.release()->deleteLater();
    emit disconnected();
    if (m_wantReconnect && m_everRegistered) {
        scheduleReconnect();
    } else if (m_wantReconnect) {
        emit statusText(tr("Connection closed before sign-in completed. Check the address and credentials."));
        m_wantReconnect = false;
    }
}

void BouncerBackend::scheduleReconnect() {
    emit statusText(tr("Connection lost. Reconnecting in %1 s...").arg(m_reconnectDelaySeconds));
    m_reconnectTimer.start(m_reconnectDelaySeconds * 1000);
    m_reconnectDelaySeconds = qMin(m_reconnectDelaySeconds * 2, 30);
}

void BouncerBackend::sendLine(const std::string& line) {
    if (!m_socket) return;
    m_socket->write(line.data(), static_cast<qint64>(line.size()));
    m_socket->write("\r\n");
}

void BouncerBackend::onEvent(const lurker::IrcEvent& event) {
    if (event.type == IrcEventType::Registered) {
        m_everRegistered = true;
        m_reconnectDelaySeconds = 2;
        emit connectedAndRegistered();
        // After an unplanned drop, pick the same channels back up. The Lurker
        // bouncer re-attaches us to everything by itself; a plain ircd needs
        // the JOINs. Joining a channel we are already in is a no-op.
        for (const QString& channel : m_rejoinChannels) m_session->join(channel.toStdString());
        m_rejoinChannels.clear();
    }
    if (event.type == IrcEventType::SaslFail) {
        emit statusText(tr("Sign-in failed: %1").arg(QString::fromStdString(event.text)));
        m_wantReconnect = false;  // retrying a bad credential would lock the account out
    }
    emit eventReceived(event);
}

}  // namespace lurker::backend
