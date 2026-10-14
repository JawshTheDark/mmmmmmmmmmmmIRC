// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Headless development probe for the bouncer backend: connect, sign in,
// report what the session learned (caps, ISUPPORT, channels, members), then
// disconnect. Sends no messages. The secret is read from a file so it never
// appears on a command line.
//
//   lurker-bouncer-probe --host chat.example --port 6697 --nick jawsh
//       --user jawsh --pass-file .secrets/token [--pin <sha256>] [--no-tls]
//       [--insecure] [--wait-seconds 8]   (one command line)

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include "backend/BouncerBackend.h"

using lurker::IrcEvent;
using lurker::IrcEventType;
using lurker::backend::BouncerBackend;
using lurker::backend::ConnectSettings;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({
        {"host", "Server host", "host"},
        {"port", "Server port (default 6697)", "port", "6697"},
        {"nick", "Nick", "nick"},
        {"user", "Username (Lurker: user or user/network)", "user"},
        {"pass-file", "File holding the SASL password/token", "path"},
        {"no-tls", "Connect without TLS"},
        {"insecure", "Continue past TLS certificate errors (prints the fingerprint)"},
        {"pin", "Accept exactly this certificate SHA-256 fingerprint", "sha256"},
        {"wait-seconds", "How long to observe after sign-in (default 8)", "n", "8"},
    });
    parser.process(app);
    if (!parser.isSet("host") || !parser.isSet("nick")) {
        out << "need --host and --nick\n";
        return 2;
    }

    ConnectSettings settings;
    settings.host = parser.value("host");
    settings.port = static_cast<quint16>(parser.value("port").toUInt());
    settings.tls = !parser.isSet("no-tls");
    settings.ignoreTlsErrors = parser.isSet("insecure");
    settings.pinnedSha256 = parser.value("pin");
    settings.nick = parser.value("nick");
    settings.username = parser.value("user");
    if (parser.isSet("pass-file")) {
        QFile f(parser.value("pass-file"));
        if (!f.open(QIODevice::ReadOnly)) {
            out << "cannot read pass file\n";
            return 2;
        }
        settings.password = QString::fromUtf8(f.readAll()).trimmed();
    }

    BouncerBackend backend;
    int exitCode = 1;

    QObject::connect(&backend, &BouncerBackend::statusText, &app,
                     [&](const QString& t) { out << "[status] " << t << "\n"; out.flush(); });
    QObject::connect(&backend, &BouncerBackend::eventReceived, &app, [&](const IrcEvent& ev) {
        switch (ev.type) {
            case IrcEventType::MotdLine:
            case IrcEventType::ServerText:
                out << "[server] " << QString::fromStdString(ev.text) << "\n";
                break;
            case IrcEventType::ErrorText:
                out << "[error]  " << QString::fromStdString(ev.text) << "\n";
                break;
            case IrcEventType::Notice:
                out << "[notice] " << QString::fromStdString(ev.nick) << ": "
                    << QString::fromStdString(ev.text) << "\n";
                break;
            case IrcEventType::SaslOk:
                out << "[sasl]   authenticated\n";
                break;
            case IrcEventType::SaslFail:
                out << "[sasl]   FAILED: " << QString::fromStdString(ev.text) << "\n";
                break;
            case IrcEventType::Join:
                out << "[join]   " << QString::fromStdString(ev.nick) << " -> "
                    << QString::fromStdString(ev.target) << (ev.self ? " (me)" : "") << "\n";
                break;
            case IrcEventType::Message:
                // History replay arrives as messages with server-time; show provenance only.
                out << "[msg]    " << QString::fromStdString(ev.target) << " <"
                    << QString::fromStdString(ev.nick) << "> (" << ev.text.size() << " chars"
                    << (ev.time.empty() ? "" : ", replay") << ")\n";
                break;
            default:
                break;
        }
        out.flush();
    });

    QObject::connect(&backend, &BouncerBackend::connectedAndRegistered, &app, [&] {
        out << "[ok] registered as " << QString::fromStdString(backend.session()->nick()) << "\n";
        out.flush();
        const int wait = parser.value("wait-seconds").toInt();
        QTimer::singleShot(wait * 1000, &app, [&] {
            auto* s = backend.session();
            if (s) {
                out << "[summary] network=" << QString::fromStdString(s->isupport().network)
                    << " casemapping=" << QString::fromStdString(s->isupport().caseMapping) << "\n";
                out << "[summary] caps:";
                for (const char* c : {"sasl", "server-time", "echo-message", "message-tags", "batch",
                                      "multi-prefix", "userhost-in-names", "away-notify", "chghost",
                                      "account-notify", "extended-join"}) {
                    if (s->capEnabled(c)) out << " " << c;
                }
                out << "\n";
                const auto names = s->channelNames();
                out << "[summary] channels (" << names.size() << "):";
                for (const auto& n : names) {
                    const auto* chan = s->channel(n);
                    out << " " << QString::fromStdString(n) << "(" << (chan ? chan->members.size() : 0) << ")";
                }
                out << "\n";
                out.flush();
                exitCode = 0;
            }
            backend.disconnectFromHost();
            QCoreApplication::exit(exitCode);
        });
    });
    QObject::connect(&backend, &BouncerBackend::disconnected, &app, [&] {
        QTimer::singleShot(0, &app, [&] { QCoreApplication::exit(exitCode); });
    });

    QTimer::singleShot(45000, &app, [&] {
        out << "[timeout] gave up after 45 s\n";
        QCoreApplication::exit(1);
    });

    backend.connectToHost(settings);
    return app.exec();
}
