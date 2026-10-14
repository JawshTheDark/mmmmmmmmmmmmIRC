// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <QApplication>
#include <QTimer>

#include "backend/BouncerBackend.h"
#include "gui/ConnectDialog.h"
#include "gui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("lurker-desktop"));
    QApplication::setOrganizationName(QStringLiteral("lurker-desktop"));

    const QStringList args = QCoreApplication::arguments();
    lurker::gui::MainWindow window;
    window.show();

    // --connect-dev: connect immediately from .secrets/dev-profile.json (local
    // dev only). Lets a headless run produce a live screenshot.
    if (args.contains(QStringLiteral("--connect-dev"))) {
        lurker::backend::ConnectSettings settings;
        if (lurker::gui::devProfileSettings(settings)) window.connectWith(settings);
    }

    // --smoke-test: quit after one event-loop pass.
    if (args.contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &app, &QCoreApplication::quit);
    }
    // --screenshot=<file>[,<delayMs>]: save a PNG of the window, then quit.
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
