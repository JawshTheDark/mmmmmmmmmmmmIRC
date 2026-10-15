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
        {QStringLiteral("solarized"), QStringLiteral("Solarized Dark")},
        {QStringLiteral("matrix"), QStringLiteral("Matrix (green)")},
        {QStringLiteral("amber"), QStringLiteral("Amber (retro)")},
        {QStringLiteral("beos"), QStringLiteral("BeOS")},
    };
}

namespace {
// A generic dark palette: `text` and `accent` let terminal-style skins (Matrix,
// Amber, Solarized) share one builder.
QPalette darkPalette(const QColor& base, const QColor& window, const QColor& text = QColor(0xE6, 0xE6, 0xE6),
                     const QColor& accent = QColor(0x3D, 0x7E, 0xFF)) {
    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window.lighter(130));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window.lighter(120));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, base.lightness() < 128 ? base : Qt::white);
    p.setColor(QPalette::Link, accent.lighter(120));
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Disabled, QPalette::Text, text.darker(170));
    return p;
}
}  // namespace

void applyTheme(QApplication& app, const QString& id) {
    app.setStyleSheet(QString());

    // Palette-based dark / terminal skins share one path (palette + a matching
    // MDI workspace backdrop, which the palette alone doesn't cover).
    struct DarkSkin {
        QColor base, window, text, accent;
    };
    DarkSkin skin;
    bool isDark = true;
    if (id == QStringLiteral("dark")) {
        skin = {QColor(0x1E, 0x20, 0x24), QColor(0x2A, 0x2D, 0x32), QColor(0xE6, 0xE6, 0xE6), QColor(0x3D, 0x7E, 0xFF)};
    } else if (id == QStringLiteral("oled")) {
        skin = {QColor(0, 0, 0), QColor(0x0A, 0x0A, 0x0A), QColor(0xE6, 0xE6, 0xE6), QColor(0x3D, 0x7E, 0xFF)};
    } else if (id == QStringLiteral("solarized")) {
        skin = {QColor(0x00, 0x2B, 0x36), QColor(0x07, 0x36, 0x42), QColor(0x93, 0xA1, 0xA1), QColor(0x26, 0x8B, 0xD2)};
    } else if (id == QStringLiteral("matrix")) {
        skin = {QColor(0x00, 0x08, 0x00), QColor(0x04, 0x12, 0x04), QColor(0x33, 0xFF, 0x66), QColor(0x00, 0xAA, 0x33)};
    } else if (id == QStringLiteral("amber")) {
        skin = {QColor(0x1A, 0x12, 0x00), QColor(0x24, 0x18, 0x00), QColor(0xFF, 0xB0, 0x00), QColor(0xCC, 0x7A, 0x00)};
    } else {
        isDark = false;
    }
    if (isDark) {
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        app.setPalette(darkPalette(skin.base, skin.window, skin.text, skin.accent));
        // Item views (the treebar, nicklists) and dock titles don't always
        // repaint from a palette change alone — especially when switching themes
        // at runtime or on Windows — so pin their colours with a stylesheet,
        // which forces a repolish. (Per-channel chat backgrounds are set on the
        // QTextBrowser itself, a widget-level stylesheet that still wins.)
        app.setStyleSheet(
            QStringLiteral("QTreeView,QTreeWidget,QListView,QListWidget{background:%1;color:%2;}"
                           "QHeaderView::section{background:%3;color:%2;}"
                           "QDockWidget{color:%2;}"
                           "QDockWidget::title{background:%3;}")
                .arg(skin.base.name(), skin.text.name(), skin.window.name()));
        // The MDI workspace backdrop is set programmatically (MainWindow reacts
        // to the palette change) because QMdiArea ignores a stylesheet background.
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
