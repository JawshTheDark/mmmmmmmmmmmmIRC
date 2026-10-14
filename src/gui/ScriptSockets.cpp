// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ScriptSockets.h"

#include <QSslSocket>

namespace lurker::gui {

ScriptSockets::ScriptSockets(QObject* parent) : QObject(parent) {}

void ScriptSockets::open(const QString& name, const QString& host, int port, bool ssl) {
    close(name);
    auto* sk = new Sock;
    sk->socket = new QSslSocket(this);
    m_socks.insert(name, sk);
    QSslSocket* s = sk->socket;

    connect(s, &QSslSocket::connected, this, [this, name] {
        if (Sock* sock = m_socks.value(name)) sock->connected = true;
        if (onEvent) onEvent(name, QStringLiteral("SOCKOPEN"));
    });
    connect(s, &QSslSocket::encrypted, this, [this, name] {
        if (Sock* sock = m_socks.value(name)) sock->connected = true;
        if (onEvent) onEvent(name, QStringLiteral("SOCKOPEN"));
    });
    connect(s, &QSslSocket::readyRead, this, [this, name, s] {
        if (Sock* sock = m_socks.value(name)) sock->buffer += s->readAll();
        if (onEvent) onEvent(name, QStringLiteral("SOCKREAD"));
    });
    connect(s, &QSslSocket::disconnected, this, [this, name] {
        if (onEvent) onEvent(name, QStringLiteral("SOCKCLOSE"));
    });
    connect(s, &QSslSocket::errorOccurred, this, [this, name, s](QAbstractSocket::SocketError) {
        if (Sock* sock = m_socks.value(name)) {
            sock->error = s->errorString();
            // A failure before connect reports via SOCKOPEN (mIRC: $sockerr set).
            if (onEvent) onEvent(name, sock->connected ? QStringLiteral("SOCKCLOSE") : QStringLiteral("SOCKOPEN"));
        }
    });
    if (ssl) s->connectToHostEncrypted(host, static_cast<quint16>(port));
    else s->connectToHost(host, static_cast<quint16>(port));
}

void ScriptSockets::writeData(const QString& name, const QString& data, bool newline) {
    if (Sock* sock = m_socks.value(name)) {
        sock->socket->write(data.toUtf8());
        if (newline) sock->socket->write("\r\n");
    }
}

void ScriptSockets::close(const QString& name) {
    if (Sock* sock = m_socks.take(name)) {
        sock->socket->disconnect(this);
        sock->socket->abort();
        sock->socket->deleteLater();
        delete sock;
    }
}

QString ScriptSockets::readLine(const QString& name, bool& ok) {
    ok = false;
    Sock* sock = m_socks.value(name);
    if (!sock) return {};
    const int nl = sock->buffer.indexOf('\n');
    if (nl < 0) return {};
    QByteArray line = sock->buffer.left(nl);
    sock->buffer.remove(0, nl + 1);
    if (line.endsWith('\r')) line.chop(1);
    ok = true;
    return QString::fromUtf8(line);
}

QString ScriptSockets::info(const QString& name, const QString& prop) const {
    Sock* sock = m_socks.value(name);
    if (!sock) return {};
    if (prop == QLatin1String("err")) return sock->error.isEmpty() ? QStringLiteral("0") : QStringLiteral("1");
    if (prop == QLatin1String("status")) {
        switch (sock->socket->state()) {
            case QAbstractSocket::ConnectedState: return QStringLiteral("active");
            case QAbstractSocket::HostLookupState:
            case QAbstractSocket::ConnectingState: return QStringLiteral("connecting");
            default: return QStringLiteral("inactive");
        }
    }
    if (prop == QLatin1String("ip")) return sock->socket->peerAddress().toString();
    return name;
}

}  // namespace lurker::gui
