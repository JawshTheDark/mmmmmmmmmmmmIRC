// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#pragma once

#include <QHash>
#include <QMainWindow>

#include "core/IrcEvent.h"
#include "gui/ChatView.h"

class QAction;
class QActionGroup;
class QMdiArea;
class QMdiSubWindow;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace lurker::backend {
class BouncerBackend;
struct ConnectSettings;
}

namespace lurker::gui {

// The application frame: an MDI workspace of ChatViews (status, channels,
// queries), a treebar and switchbar listing them, menus, and one bouncer
// connection driving it all.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void showConnectDialog();
    void connectWith(const lurker::backend::ConnectSettings& settings);

private:
    struct Buffer {
        ChatView* view = nullptr;
        QMdiSubWindow* sub = nullptr;
        QAction* switchAction = nullptr;
        QTreeWidgetItem* treeItem = nullptr;
        ChatView::Kind kind = ChatView::Kind::Channel;
        QString display;  // channel/nick as shown
        QString key;      // casefolded identity ("" => status)
    };

    Buffer& ensureBuffer(ChatView::Kind kind, const QString& display, const QString& key);
    Buffer* findBuffer(const QString& key);
    QString statusKey() const { return QStringLiteral(":status:"); }
    QString bufferKeyFor(const QString& target) const;  // casefolded via the session

    void onEvent(const lurker::IrcEvent& event);
    void onStatusText(const QString& text);
    void onInput(Buffer* buffer, const QString& text);
    void refreshNicklist(const QString& channelDisplay);
    void closeBuffer(Buffer* buffer);

    QString timestampFor(const std::string& isoTime) const;

    QMdiArea* m_mdi = nullptr;
    QTreeWidget* m_treebar = nullptr;
    QToolBar* m_switchbar = nullptr;
    QActionGroup* m_switchGroup = nullptr;
    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;

    lurker::backend::BouncerBackend* m_backend = nullptr;
    QHash<QString, Buffer> m_buffers;  // key -> buffer
    bool m_closing = false;            // true once teardown starts; silences buffer cleanup
};

}  // namespace lurker::gui
