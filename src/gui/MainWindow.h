// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#pragma once

#include <QHash>
#include <QMainWindow>
#include <memory>

#include "backend/ChatBackend.h"
#include "core/IrcEvent.h"
#include "core/msl/Interpreter.h"
#include "gui/ChatView.h"
#include "gui/GuiScriptHost.h"

class QAction;
class QActionGroup;
class QMdiArea;
class QMdiSubWindow;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;

namespace lurker::gui {

// The application frame. Holds one ChatBackend (bouncer or Lurker), a tree of
// networks → buffers, an MDI workspace of ChatViews, and a switchbar. Buffers
// are keyed by (networkId, folded target) so several networks coexist.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void showConnectDialog();
    void showOptions(const QString& page = QString());  // open Options, optionally at a page

protected:
    void changeEvent(QEvent* event) override;  // re-tint the MDI on theme/palette change

public:
    void showJoinDialog();        // prompt for a channel and JOIN it
    void requestChannelList();    // /LIST on the active network
    void showAddressBook();       // favourite channels (add / remove / join)
    void showDccInfo();           // DCC status (transfers proxied through Lurker)
    void openLogsFolder();        // reveal the logging folder
    void showAbout();             // About box
    void promptLoadScript();      // file picker → loadScriptFile
    // Adopts a backend (ownership transferred), wires it, and starts rendering.
    void useBackend(lurker::backend::ChatBackend* backend);
    // Open (creating if needed) and focus a conversation; also used by dev flags.
    void openConversation(int net, const QString& target);
    // Run an MSL line in the active (or status) window — used by // input and --msl.
    void runScript(const QString& line);
    void loadScriptFile(const QString& path);
    void reloadScripts();        // clear + reload all tracked script files
    void openScriptEditor();     // Scripts Editor window
    QStringList scriptFiles() const { return m_scriptFiles; }
    QString scriptSummary() const;  // "N aliases, N events, N dialogs"
    // Dev: inject a synthetic incoming message (renders + fires on TEXT).
    void simulateMessage(int net, const QString& nick, const QString& target, const QString& text);

private:
    struct Buffer {
        ChatView* view = nullptr;
        QMdiSubWindow* sub = nullptr;
        QAction* switchAction = nullptr;
        QTreeWidgetItem* treeItem = nullptr;
        ChatView::Kind kind = ChatView::Kind::Channel;
        int networkId = -1;
        QString display;        // channel/nick as shown
        QString key;            // "net\x1ffoldedtarget"
        QStringList memberNicks; // current nicklist (no sigils), for tab-completion
        bool hydrated = false;   // history has been requested for this buffer
        qlonglong lastMessageId = 0;  // newest persisted id seen here (for mark-read)
        int unread = 0;          // server-authoritative unread count
    };

    QString keyFor(int net, const QString& target) const;
    QString statusKey() const { return QStringLiteral("-1\x1f:status:"); }
    QString serverKeyFor(int net) const { return keyFor(net, QStringLiteral(":server:")); }

    Buffer& ensureBuffer(int net, ChatView::Kind kind, const QString& display, const QString& key);
    Buffer* findBuffer(const QString& key);
    QTreeWidgetItem* networkNode(int net);

    void onStatusText(const QString& text);
    void onNetworkUpdated(const lurker::backend::NetworkInfo& info);
    void onBufferOpened(int net, const QString& target, int kind);
    void onEvent(const lurker::IrcEvent& event);
    void onMembersReplaced(int net, const QString& target, const QList<lurker::backend::DisplayMember>& members);
    void onTopicChanged(int net, const QString& target, const QByteArray& topic);
    void onReadStateChanged(int net, const QString& target, int unread, int highlights);
    void updateBufferLabel(Buffer* buffer);
    bool bufferIsActive(const Buffer& buffer) const;
    void noteMessage(Buffer* buffer, long long id);  // track newest id + mark-read if active
    void setupScripting();
    void setupToolbar();         // the top mIRC-style icon toolbar
    void updateMdiBackground();  // tint the MDI workspace to the active theme
    Buffer* activeBuffer();
    void echoToWindow(int net, const QString& window, const QString& text);
    void dispatchScriptEvent(const lurker::IrcEvent& event);
    void onInput(Buffer* buffer, const QString& text);
    void closeBuffer(Buffer* buffer);
    void activateBuffer(Buffer* buffer);  // restore, focus, and hydrate on first view
    void hydrateBuffer(Buffer* buffer);   // fetch backlog once

    Buffer* routeTarget(int net, const QString& target, ChatView::Kind fallback);
    QString timestampFor(const std::string& isoTime) const;

    QMdiArea* m_mdi = nullptr;
    QTreeWidget* m_treebar = nullptr;
    QToolBar* m_switchbar = nullptr;
    QToolBar* m_toolbar = nullptr;   // top icon toolbar
    QActionGroup* m_switchGroup = nullptr;
    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;
    QAction* m_tbDisconnect = nullptr;  // toolbar twin of m_disconnectAction

    lurker::backend::ChatBackend* m_backend = nullptr;
    QHash<QString, Buffer> m_buffers;              // key -> buffer
    QHash<int, QTreeWidgetItem*> m_networkNodes;   // networkId -> tree node (server buffer item)
    QHash<int, QString> m_networkNames;            // networkId -> display name
    QHash<int, QString> m_networkNicks;            // networkId -> our nick (for $me)
    bool m_closing = false;

    GuiScriptHost m_scriptHost;
    std::unique_ptr<lurker::msl::Interpreter> m_msl;
    class ScriptSockets* m_sockets = nullptr;
    QStringList m_scriptFiles;  // loaded .mrc paths, for the editor + reload
    QHash<QString, class ScriptDialog*> m_dialogs;  // open script dialogs by instance name
    QHash<QString, class QTimer*> m_scriptTimers;   // /timer instances by name
    int m_timerSeq = 0;
    QHash<QString, class PictureWindow*> m_pictureWindows;  // /window -p canvases
    class PictureWindow* ensurePictureWindow(const QString& name);
    lurker::msl::ScriptTarget m_eventContext;  // active target while an on-event runs
    bool m_inEvent = false;
};

}  // namespace lurker::gui
