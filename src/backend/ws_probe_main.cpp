// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Headless probe for the Lurker WebSocket/REST backend: fetch config, sign in
// (token file or username+password), list networks, open the WS, and summarize
// the snapshot burst (networks, buffers, backlog counts). Read-only.
//
//   lurker-ws-probe --host chat.irc.so --token-file .secrets/ws-token
//       [--user jawsh --pass-file .secrets/password] [--wait-seconds 8]

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QTimer>

#include "backend/LurkerClient.h"

using lurker::backend::LurkerClient;
using lurker::backend::LurkerConfig;
using lurker::backend::LurkerConnectSettings;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {"host", "Lurker host", "host"},
        {"token-file", "File with a minted session / OAuth token", "path"},
        {"user", "Username (to mint a token when no token file)", "user"},
        {"pass-file", "File with the password (to mint a token)", "path"},
        {"no-tls", "Use http/ws instead of https/wss"},
        {"wait-seconds", "Observe this long after the snapshot (default 8)", "n", "8"},
    });
    parser.process(app);
    if (!parser.isSet("host")) {
        out << "need --host\n";
        return 2;
    }

    LurkerConnectSettings s;
    s.host = parser.value("host");
    s.tls = !parser.isSet("no-tls");
    if (parser.isSet("token-file")) {
        QFile f(parser.value("token-file"));
        if (f.open(QIODevice::ReadOnly)) s.token = QString::fromUtf8(f.readAll()).trimmed();
    }
    if (s.token.isEmpty() && parser.isSet("user") && parser.isSet("pass-file")) {
        s.username = parser.value("user");
        QFile f(parser.value("pass-file"));
        if (f.open(QIODevice::ReadOnly)) s.password = QString::fromUtf8(f.readAll()).trimmed();
    }

    LurkerClient client;
    QHash<QString, int> networkNames;  // id -> index for display
    int backlogFrames = 0, shellFrames = 0;
    qint64 liveEvents = 0;
    int exitCode = 1;

    QObject::connect(&client, &LurkerClient::statusText, &app,
                     [&](const QString& t) { out << "[status] " << t << "\n"; out.flush(); });
    QObject::connect(&client, &LurkerClient::restError, &app, [&](const QString& t) {
        out << "[error] " << t << "\n";
        out.flush();
    });
    QObject::connect(&client, &LurkerClient::networksReceived, &app, [&](const QJsonArray& nets) {
        out << "[networks] " << nets.size() << ":";
        for (const auto& v : nets) {
            const auto o = v.toObject();
            out << " " << o.value("id").toInt() << "=" << o.value("name").toString();
        }
        out << "\n";
        out.flush();
    });
    QObject::connect(&client, &LurkerClient::connected, &app,
                     [&] { out << "[ws] connected\n"; out.flush(); });
    QObject::connect(&client, &LurkerClient::snapshotReceived, &app, [&](const QJsonObject& f) {
        const QJsonArray nets = f.value("networks").toArray();
        out << "[snapshot] protocol=" << f.value("protocolVersion").toInt()
            << " cursor=" << static_cast<qint64>(f.value("cursor").toDouble())
            << " maxUpload=" << static_cast<qint64>(f.value("maxUploadBytes").toDouble())
            << " networks=" << nets.size() << "\n";
        for (const auto& v : nets) {
            const auto o = v.toObject();
            out << "   net " << o.value("networkId").toInt() << " state=" << o.value("state").toString()
                << " nick=" << o.value("nick").toString()
                << " channels=" << o.value("channels").toArray().size() << "\n";
        }
        out.flush();
    });
    QObject::connect(&client, &LurkerClient::backlogReceived, &app, [&](const QJsonObject& f) {
        const bool shell = f.value("mode").toString() == "shell" || f.value("events").toArray().isEmpty();
        if (shell) ++shellFrames;
        else ++backlogFrames;
    });
    QObject::connect(&client, &LurkerClient::ircEvent, &app, [&](const QJsonObject&) { ++liveEvents; });
    QObject::connect(&client, &LurkerClient::backlogComplete, &app, [&] {
        out << "[snapshot] backlog-complete: " << backlogFrames << " hydrated, " << shellFrames
            << " shells; resume cursor now " << client.resumeCursor() << "\n";
        out.flush();
        const int wait = parser.value("wait-seconds").toInt();
        QTimer::singleShot(wait * 1000, &app, [&, wait] {
            out << "[summary] live events observed in " << wait << "s: " << liveEvents << "\n";
            exitCode = 0;
            client.disconnect();
            QCoreApplication::exit(exitCode);
        });
    });
    QObject::connect(&client, &LurkerClient::closed, &app, [&] { QCoreApplication::exit(exitCode); });

    QObject::connect(&client, &LurkerClient::configReceived, &app, [&](const LurkerConfig& c) {
        out << "[config] edition=" << c.edition << " protocol=" << c.protocolVersion << " features="
            << QString::fromUtf8(QJsonDocument(c.features).toJson(QJsonDocument::Compact)) << "\n";
        out.flush();
        client.connectToLurker(s);
    });

    QTimer::singleShot(45000, &app, [&] {
        out << "[timeout]\n";
        QCoreApplication::exit(1);
    });

    client.fetchConfig(s.host, s.tls);
    return app.exec();
}
