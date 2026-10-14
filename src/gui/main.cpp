// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <QApplication>
#include <QTimer>

#include "gui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("lurker-desktop"));
    QApplication::setOrganizationName(QStringLiteral("lurker-desktop"));

    lurker::gui::MainWindow window;
    window.printStatus("\x02Lurker Desktop\x02 " LURKER_DESKTOP_VERSION);
    window.printStatus("Not connected yet: sign-in arrives with the Lurker backend.");
    window.show();

    // --smoke-test: start, process one event loop pass, exit. Used to check the build runs.
    // --screenshot=<file.png>: same, but save an image of the window first (works offscreen).
    for (const QString& arg : QCoreApplication::arguments()) {
        if (arg == QStringLiteral("--smoke-test")) {
            QTimer::singleShot(0, &app, &QCoreApplication::quit);
        } else if (arg.startsWith(QStringLiteral("--screenshot="))) {
            const QString path = arg.section(QLatin1Char('='), 1);
            QTimer::singleShot(200, &app, [&window, path] {
                window.grab().save(path);
                QCoreApplication::quit();
            });
        }
    }
    return QApplication::exec();
}
