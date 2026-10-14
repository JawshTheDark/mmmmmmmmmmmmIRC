// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#pragma once

#include <QHash>
#include <QMainWindow>

class QAction;
class QActionGroup;
class QMdiArea;
class QMdiSubWindow;
class QTextBrowser;
class QToolBar;
class QTreeWidget;

namespace lurker::gui {

// The application frame: an MDI workspace of chat windows, a treebar listing
// them, a switchbar of buttons (one per window) and the usual menus and toolbar.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Creates (or returns) the status window and writes one formatted line to it.
    void printStatus(const QByteArray& utf8Line);

private:
    QMdiSubWindow* addChatWindow(const QString& title, QTextBrowser** viewOut);

    QMdiArea* m_mdi = nullptr;
    QTreeWidget* m_treebar = nullptr;
    QToolBar* m_switchbar = nullptr;
    QActionGroup* m_switchGroup = nullptr;
    QMdiSubWindow* m_statusWindow = nullptr;
    QTextBrowser* m_statusView = nullptr;
    QHash<QMdiSubWindow*, QAction*> m_switchActions;
};

}  // namespace lurker::gui
