// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// TCP/SSL sockets for MSL scripts (/sockopen, /sockwrite, /sockread, $sock).
// Each named socket buffers incoming bytes; on SOCKREAD the script pulls lines
// with /sockread. Events are reported through onEvent so the engine can fire the
// matching on SOCK* handlers.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

class QSslSocket;

namespace lurker::gui {

class ScriptSockets : public QObject {
    Q_OBJECT
public:
    explicit ScriptSockets(QObject* parent = nullptr);

    std::function<void(const QString& name, const QString& event)> onEvent;  // SOCKOPEN/SOCKREAD/SOCKCLOSE

    void open(const QString& name, const QString& host, int port, bool ssl);
    void writeData(const QString& name, const QString& data, bool newline);
    void close(const QString& name);
    QString readLine(const QString& name, bool& ok);
    QString info(const QString& name, const QString& prop) const;

private:
    struct Sock {
        QSslSocket* socket = nullptr;
        QByteArray buffer;
        QString error;
        bool connected = false;
    };
    QHash<QString, Sock*> m_socks;
};

}  // namespace lurker::gui
