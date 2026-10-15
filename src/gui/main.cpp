// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#include <QSettings>

#include "backend/LurkerChatBackend.h"
#include "gui/AppIcon.h"
#include "gui/ChatView.h"
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

// Exposes QWidget::event() (protected) so --sharetest can deliver a synthetic
// QDropEvent directly — QApplication::notify only routes DnD during a live drag.
struct TestChatView : lurker::gui::ChatView {
    using ChatView::ChatView;
    bool deliver(QEvent* e) { return event(e); }
};

// A no-op backend that announces one network + channel + line, for --multidemo.
// Two of these use the SAME local network id (1), so the demo shows the
// MultiplexBackend keeping them apart in the treebar (issue #3).
class DemoBackend : public lurker::backend::ChatBackend {
public:
    DemoBackend(QString net, QString chan) : m_net(std::move(net)), m_chan(std::move(chan)) {}
    bool isReady() const override { return true; }
    void sendMessage(int, const QString&, const QString&) override {}
    void sendAction(int, const QString&, const QString&) override {}
    void sendNotice(int, const QString&, const QString&) override {}
    void sendRaw(int, const QString&) override {}
    void joinChannel(int, const QString&, const QString&) override {}
    void partChannel(int, const QString&, const QString&) override {}
    void setTopic(int, const QString&, const QString&) override {}
    void setAway(int, const QString&) override {}
    void clearAway(int) override {}
    void sendCtcp(int, const QString&, const QString&, const QString&) override {}
    void requestHistory(int, const QString&) override {}
    void markRead(int, const QString&, qlonglong) override {}
    void disconnectAll() override {}
    void start() {
        lurker::backend::NetworkInfo ni;
        ni.id = 1;  // both demo backends use id 1 on purpose
        ni.name = m_net;
        ni.state = QStringLiteral("connected");
        ni.nick = QStringLiteral("jawsh");
        emit networkUpdated(ni);
        emit bufferOpened(1, m_chan, 0);
        lurker::IrcEvent ev;
        ev.type = lurker::IrcEventType::Message;
        ev.networkId = 1;
        ev.nick = "neighbour";
        ev.target = m_chan.toStdString();
        ev.text = ("hello from " + m_net).toStdString();
        emit event(ev);
    }

private:
    QString m_net, m_chan;
};
}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("mmmmmmmmmmmmIRC"));
    QApplication::setOrganizationName(QStringLiteral("mmmmmmmmmmmmIRC"));
    QApplication::setDesktopFileName(QStringLiteral("mmmmmmmmmmmmirc"));  // matches the .desktop basename
    // Store settings in a portable .ini config file (not the Windows registry),
    // so config — loaded scripts, variables, theme, connection — lives in one
    // visible file that persists across versions and is easy to back up.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QApplication::setApplicationDisplayName(QStringLiteral("mmmmmmmmmmmmIRC"));
    // One-time migration from the old "lurker-desktop" config location so a
    // user's themes, identity, favourites and connection carry over the rename.
    {
        QSettings now;  // new org/app (mmmmmmmmmmmmIRC), IniFormat
        if (!now.value(QStringLiteral("_migratedFromLurkerDesktop")).toBool() && now.allKeys().isEmpty()) {
            QSettings old(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lurker-desktop"),
                          QStringLiteral("lurker-desktop"));
            for (const QString& k : old.allKeys()) now.setValue(k, old.value(k));
        }
        now.setValue(QStringLiteral("_migratedFromLurkerDesktop"), true);
    }
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
    // --sharetest: drive real QDropEvents through a ChatView and confirm the
    // share signals fire (offscreen-safe self-check for issue #4's drag-drop path).
    if (args.contains(QStringLiteral("--sharetest"))) {
        using lurker::gui::ChatView;
        TestChatView view(ChatView::Kind::Channel);
        bool gotFiles = false, gotImage = false;
        QStringList files;
        QObject::connect(&view, &ChatView::shareFiles, [&](const QStringList& p) {
            gotFiles = true;
            files = p;
        });
        QObject::connect(&view, &ChatView::shareImage, [&](const QImage&) { gotImage = true; });

        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/mmirc-XXXXXX.txt"));
        if (!tmp.open()) return 1;
        tmp.write("hi");
        tmp.flush();
        QMimeData m1;
        m1.setUrls({QUrl::fromLocalFile(tmp.fileName())});
        QDropEvent de1(QPointF(10, 10), Qt::CopyAction, &m1, Qt::LeftButton, Qt::NoModifier);
        view.deliver(&de1);

        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(Qt::red);
        QMimeData m2;
        m2.setImageData(img);
        QDropEvent de2(QPointF(10, 10), Qt::CopyAction, &m2, Qt::LeftButton, Qt::NoModifier);
        view.deliver(&de2);

        const bool ok = gotFiles && gotImage && files.size() == 1 && files.value(0) == tmp.fileName();
        fprintf(stderr, "sharetest: files=%d image=%d path=%s => %s\n", gotFiles, gotImage,
                qUtf8Printable(files.value(0)), ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
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
        } else if (arg == QStringLiteral("--previewdrift")) {
            QTimer::singleShot(500, &window, [&window] { window.previewDriftDemo(); });
        } else if (arg == QStringLiteral("--dccdemo")) {
            QTimer::singleShot(500, &window, [&window] { window.dccDemo(); });
        } else if (arg == QStringLiteral("--urldemo")) {
            QTimer::singleShot(500, &window, [&window] { window.urlDemo(); });
        } else if (arg == QStringLiteral("--ccdemo")) {
            QTimer::singleShot(500, &window, [&window] { window.channelCentralDemo(); });
        } else if (arg == QStringLiteral("--cascadedemo")) {
            QTimer::singleShot(500, &window, [&window] { window.cascadeDemo(); });
        } else if (arg == QStringLiteral("--reactdemo")) {
            QTimer::singleShot(500, &window, [&window] { window.reactionsDemo(); });
        } else if (arg == QStringLiteral("--multidemo")) {
            QTimer::singleShot(500, &window, [&window] {
                auto* d1 = new DemoBackend(QStringLiteral("Lurker"), QStringLiteral("#lurker"));
                auto* d2 = new DemoBackend(QStringLiteral("Libera.Chat (direct)"), QStringLiteral("#linux"));
                window.useBackend(d1);  // child 0
                window.useBackend(d2);  // child 1 — same local id 1, namespaced by the mux
                d1->start();
                d2->start();
            });
        } else if (arg == QStringLiteral("--addressbook")) {
            QTimer::singleShot(500, &window, [&window] { window.showAddressBook(); });
        } else if (arg == QStringLiteral("--serverlist")) {
            QTimer::singleShot(500, &window, [&window] { window.showServerList(); });
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
