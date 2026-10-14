// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDockWidget>
#include <QFontDatabase>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextBrowser>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "gui/FormattedText.h"

namespace lurker::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Lurker Desktop"));
    resize(1100, 720);

    m_mdi = new QMdiArea(this);
    m_mdi->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_mdi->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setCentralWidget(m_mdi);

    // Treebar: every open window, grouped by network.
    auto* treeDock = new QDockWidget(tr("Treebar"), this);
    treeDock->setObjectName(QStringLiteral("treebar"));
    treeDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    m_treebar = new QTreeWidget(treeDock);
    m_treebar->setHeaderHidden(true);
    treeDock->setWidget(m_treebar);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);

    // Switchbar: one checkable button per window, in opening order.
    m_switchbar = new QToolBar(tr("Switchbar"), this);
    m_switchbar->setObjectName(QStringLiteral("switchbar"));
    m_switchbar->setMovable(true);
    addToolBar(Qt::BottomToolBarArea, m_switchbar);
    m_switchGroup = new QActionGroup(this);

    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);
    auto* window = menuBar()->addMenu(tr("&Window"));
    window->addAction(tr("&Cascade"), m_mdi, &QMdiArea::cascadeSubWindows);
    window->addAction(tr("&Tile"), m_mdi, &QMdiArea::tileSubWindows);
    window->addAction(treeDock->toggleViewAction());
    window->addAction(m_switchbar->toggleViewAction());

    connect(m_mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow* w) {
        if (auto* action = m_switchActions.value(w)) action->setChecked(true);
    });

    statusBar()->showMessage(tr("Not connected"));
}

QMdiSubWindow* MainWindow::addChatWindow(const QString& title, QTextBrowser** viewOut) {
    auto* view = new QTextBrowser;
    view->setOpenExternalLinks(true);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    auto* sub = m_mdi->addSubWindow(view);
    sub->setWindowTitle(title);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    sub->resize(700, 420);
    sub->show();

    auto* item = new QTreeWidgetItem(m_treebar, {title});
    auto* action = m_switchbar->addAction(title);
    action->setCheckable(true);
    m_switchGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, sub] { m_mdi->setActiveSubWindow(sub); });
    m_switchActions.insert(sub, action);

    connect(sub, &QObject::destroyed, this, [this, sub, item] {
        if (auto* a = m_switchActions.take(sub)) a->deleteLater();
        delete item;
        if (sub == m_statusWindow) {
            m_statusWindow = nullptr;
            m_statusView = nullptr;
        }
    });

    *viewOut = view;
    return sub;
}

void MainWindow::printStatus(const QByteArray& utf8Line) {
    if (!m_statusWindow) m_statusWindow = addChatWindow(tr("Status"), &m_statusView);
    QTextCursor cursor(m_statusView->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_statusView->document()->isEmpty()) cursor.insertBlock();
    const QPalette pal = m_statusView->palette();
    insertFormatted(cursor, utf8Line, {pal.color(QPalette::Text), pal.color(QPalette::Base)});
}

}  // namespace lurker::gui
