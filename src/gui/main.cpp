// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

#include <QSettings>

#include "backend/LurkerChatBackend.h"
#include "gui/AppIcon.h"
#include "gui/ConnectDialog.h"
#include "gui/MainWindow.h"
#include "gui/OptionsDialog.h"
#include "gui/Theme.h"

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
    // Store settings in a portable .ini config file (not the Windows registry),
    // so config — loaded scripts, variables, theme, connection — lives in one
    // visible file that persists across versions and is easy to back up.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QApplication::setApplicationDisplayName(QStringLiteral("Lurker Desktop"));
    QApplication::setWindowIcon(lurker::gui::appIcon());

    const QStringList args = QCoreApplication::arguments();
    // --export-icon=<path>: render the app icon to a PNG (used by packaging) and exit.
    for (const QString& a : args) {
        if (a.startsWith(QStringLiteral("--export-icon="))) {
            const QString path = a.section(QLatin1Char('='), 1);
            const int sz = path.contains(QLatin1Char(',')) ? path.section(QLatin1Char(','), 1, 1).toInt() : 256;
            lurker::gui::appIconPixmap(sz > 0 ? sz : 256).save(path.section(QLatin1Char(','), 0, 0));
            return 0;
        }
    }
    // Apply the saved theme (or --theme=<id> for dev) before building the window.
    QString themeId = QSettings().value(QStringLiteral("theme"), QStringLiteral("default")).toString();
    for (const QString& a : args) {
        if (a.startsWith(QStringLiteral("--theme="))) themeId = a.section(QLatin1Char('='), 1);
    }
    lurker::gui::applyTheme(app, themeId);

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
    } else if (!args.contains(QStringLiteral("--smoke-test")) && !args.contains(QStringLiteral("--no-autoconnect")) &&
               QSettings().value(QStringLiteral("connect/onStartup"), true).toBool()) {
        // Auto-connect at launch to the last-saved server (if creds were remembered).
        if (auto* backend = lurker::gui::ConnectDialog::createBackendFromSettings(&window)) window.useBackend(backend);
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
    // --script=<path>: load a remote .mrc. --simulate=net|nick|target|text: inject a message.
    for (const QString& arg : args) {
        if (arg.startsWith(QStringLiteral("--msl="))) {
            const QString script = arg.section(QLatin1Char('='), 1);
            QTimer::singleShot(300, &window, [&window, script] { window.runScript(script); });
        } else if (arg.startsWith(QStringLiteral("--script="))) {
            const QString path = arg.section(QLatin1Char('='), 1);
            QTimer::singleShot(200, &window, [&window, path] { window.loadScriptFile(path); });
        } else if (arg == QStringLiteral("--options") || arg.startsWith(QStringLiteral("--options="))) {
            const QString page = arg.contains(QLatin1Char('=')) ? arg.section(QLatin1Char('='), 1) : QString();
            QTimer::singleShot(500, &window, [&window, page] {
                lurker::gui::OptionsDialog* d = new lurker::gui::OptionsDialog(&window);
                d->setAttribute(Qt::WA_DeleteOnClose);
                if (!page.isEmpty()) d->selectPage(page);
                d->show();
            });
        } else if (arg == QStringLiteral("--scripteditor")) {
            QTimer::singleShot(600, &window, [&window] { window.openScriptEditor(); });
        } else if (arg == QStringLiteral("--listdemo")) {
            QTimer::singleShot(500, &window, [&window] { window.simulateListReply(); });
        } else if (arg == QStringLiteral("--previewdemo")) {
            QTimer::singleShot(500, &window, [&window] { window.previewDemo(); });
        } else if (arg == QStringLiteral("--dccdemo")) {
            QTimer::singleShot(500, &window, [&window] { window.dccDemo(); });
        } else if (arg == QStringLiteral("--urldemo")) {
            QTimer::singleShot(500, &window, [&window] { window.urlDemo(); });
        } else if (arg.startsWith(QStringLiteral("--importmirc="))) {
            const QString dir = arg.section(QLatin1Char('='), 1);
            QTimer::singleShot(400, &window, [&window, dir] { window.importMircFolder(dir); });
        } else if (arg == QStringLiteral("--connect")) {
            QTimer::singleShot(500, &window, [&window] { window.showConnectDialog(); });
        } else if (arg.startsWith(QStringLiteral("--simulate="))) {
            const QStringList f = arg.section(QLatin1Char('='), 1).split(QLatin1Char('|'));
            if (f.size() >= 4) {
                QTimer::singleShot(400, &window, [&window, f] {
                    window.simulateMessage(f[0].toInt(), f[1], f[2], f.mid(3).join(QLatin1Char('|')));
                    window.openConversation(f[0].toInt(), f[2]);
                });
            }
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
                // Prefer a visible script dialog if one is open, else the window.
                QWidget* target = &window;
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (w != &window && w->isVisible() && qobject_cast<QDialog*>(w)) {
                        target = w;
                        break;
                    }
                }
                target->grab().save(path);
                QCoreApplication::quit();
            });
        }
    }
    return QApplication::exec();
}
