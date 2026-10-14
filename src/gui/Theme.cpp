// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

namespace lurker::gui {

QList<Theme> availableThemes() {
    return {
        {QStringLiteral("default"), QStringLiteral("Default")},
        {QStringLiteral("dark"), QStringLiteral("Dark")},
        {QStringLiteral("oled"), QStringLiteral("OLED (black)")},
        {QStringLiteral("beos"), QStringLiteral("BeOS")},
    };
}

namespace {
QPalette darkPalette(const QColor& base, const QColor& window) {
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, QColor(0xE6, 0xE6, 0xE6));
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window.lighter(130));
    p.setColor(QPalette::Text, QColor(0xE6, 0xE6, 0xE6));
    p.setColor(QPalette::Button, window.lighter(120));
    p.setColor(QPalette::ButtonText, QColor(0xE6, 0xE6, 0xE6));
    p.setColor(QPalette::Highlight, QColor(0x3D, 0x7E, 0xFF));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x6C, 0xA0, 0xFF));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, QColor(0xE6, 0xE6, 0xE6));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x8A, 0x8A, 0x8A));
    return p;
}
}  // namespace

void applyTheme(QApplication& app, const QString& id) {
    app.setStyleSheet(QString());

    if (id == QStringLiteral("dark") || id == QStringLiteral("oled")) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        const QColor base = id == QStringLiteral("oled") ? QColor(0, 0, 0) : QColor(0x1E, 0x20, 0x24);
        const QColor window = id == QStringLiteral("oled") ? QColor(0x0A, 0x0A, 0x0A) : QColor(0x2A, 0x2D, 0x32);
        app.setPalette(darkPalette(base, window));
        return;
    }

    if (id == QStringLiteral("beos")) {
        // BeOS: light grey chrome, yellow active window tabs, flat bevels.
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        QPalette p;
        const QColor grey(0xD8, 0xD8, 0xD8), light(0xEC, 0xEC, 0xEC), yellow(0xFF, 0xD8, 0x00);
        p.setColor(QPalette::Window, grey);
        p.setColor(QPalette::WindowText, QColor(0x10, 0x10, 0x10));
        p.setColor(QPalette::Base, light);
        p.setColor(QPalette::AlternateBase, grey);
        p.setColor(QPalette::Text, QColor(0x10, 0x10, 0x10));
        p.setColor(QPalette::Button, grey);
        p.setColor(QPalette::ButtonText, QColor(0x10, 0x10, 0x10));
        p.setColor(QPalette::Highlight, yellow);
        p.setColor(QPalette::HighlightedText, QColor(0x10, 0x10, 0x10));
        p.setColor(QPalette::Link, QColor(0x00, 0x33, 0xAA));
        app.setPalette(p);
        app.setStyleSheet(QStringLiteral(R"(
            QMainWindow, QDialog, QWidget { background: #d8d8d8; }
            QMdiArea { background: #a8a8a8; }
            QMdiSubWindow::title { background: #ffd800; color: #202000; }
            QMdiSubWindow { border: 1px solid #808080; }
            QAbstractItemView, QTextBrowser, QPlainTextEdit, QLineEdit {
                background: #fbfbf2; border: 1px solid #808080;
            }
            QPushButton {
                background: #e4e4e4; border: 2px solid; border-color: #ffffff #808080 #808080 #ffffff;
                padding: 2px 10px; border-radius: 2px;
            }
            QPushButton:pressed { border-color: #808080 #ffffff #ffffff #808080; background: #d0d0d0; }
            QToolBar, QMenuBar, QStatusBar { background: #d8d8d8; }
            QDockWidget::title { background: #ffd800; padding: 3px; }
            QTreeWidget::item:selected, QListWidget::item:selected { background: #ffd800; color: #202000; }
        )"));
        return;
    }

    // Default: the platform's native light look.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(app.style()->standardPalette());
}

}  // namespace lurker::gui
