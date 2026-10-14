// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include "backend/LurkerChatBackend.h"
#include "gui/MainWindow.h"

namespace {
// Locate and load .secrets/dev-profile.json for a local Lurker WS connection.
bool devLurkerSettings(lurker::backend::LurkerConnectSettings& out) {
    QDir dir(QDir::currentPath());
    QString path;
    for (int i = 0; i < 6; ++i) {
        const QString c = dir.filePath(QStringLiteral(".secrets/dev-profile.json"));
        if (QFile::exists(c)) {
            path = c;
            break;
        }
        if (!dir.cdUp()) break;
    }
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QDir base = QFileInfo(path).dir();
    out.host = o.value(QStringLiteral("host")).toString();
    out.tls = o.value(QStringLiteral("tls")).toBool(true);
    const QJsonObject ws = o.value(QStringLiteral("ws")).toObject();
    const QString tokenFile = ws.value(QStringLiteral("wsTokenFile")).toString();
    if (!tokenFile.isEmpty()) {
        QFile tf(base.filePath(QFileInfo(tokenFile).fileName()));
        if (tf.open(QIODevice::ReadOnly)) out.token = QString::fromUtf8(tf.readAll()).trimmed();
    }
    return !out.host.isEmpty() && !out.token.isEmpty();
}
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("lurker-desktop"));
    QApplication::setOrganizationName(QStringLiteral("lurker-desktop"));

    const QStringList args = QCoreApplication::arguments();
    lurker::gui::MainWindow window;
    window.show();

    // --connect-dev: connect to Lurker over WS from .secrets/dev-profile.json.
    if (args.contains(QStringLiteral("--connect-dev"))) {
        lurker::backend::LurkerConnectSettings s;
        if (devLurkerSettings(s)) {
            auto* backend = new lurker::backend::LurkerChatBackend;
            window.useBackend(backend);
            backend->connectToLurker(s);
        }
    }

    // --open=NET/TARGET: after connecting, open and hydrate that conversation
    // (dev aid so a headless run can show a channel's backlog).
    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("--open="))) {
            const QString spec = arg.section(QLatin1Char('='), 1);
            const int net = spec.section(QLatin1Char('/'), 0, 0).toInt();
            const QString target = spec.section(QLatin1Char('/'), 1);
            QTimer::singleShot(8000, &window, [&window, net, target] { window.openConversation(net, target); });
        }
    }

    // --msl=<script>: run an MSL script in the status window (demo/dev). Repeatable.
    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("--msl="))) {
            const QString script = arg.section(QLatin1Char('='), 1);
            QTimer::singleShot(300, &window, [&window, script] { window.runScript(script); });
        }
    }

    if (args.contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("--screenshot="))) {
            const QString spec = arg.section(QLatin1Char('='), 1);
            const QString path = spec.section(QLatin1Char(','), 0, 0);
            const int delayMs = spec.contains(QLatin1Char(',')) ? spec.section(QLatin1Char(','), 1, 1).toInt() : 300;
            QTimer::singleShot(delayMs, &app, [&window, path] {
                window.grab().save(path);
                QCoreApplication::quit();
            });
        }
    }
    return QApplication::exec();
}
