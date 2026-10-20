// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <memory>

#include "backend/ChatBackend.h"

namespace lurker::backend {
class MultiplexBackend;
}
#include "core/IrcEvent.h"
#include "core/msl/Interpreter.h"
#include "gui/ChatView.h"
#include "gui/GuiScriptHost.h"
#include "gui/UrlCatcherWindow.h"  // CaughtUrl, kept by value in the catch history

class QAction;
class QActionGroup;
class QImage;
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
    void showServerList();  // the saved-server manager (incl. imported servers)
    void showOptions(const QString& page = QString());  // open Options, optionally at a page

protected:
    void changeEvent(QEvent* event) override;  // re-tint the MDI on theme/palette change
    void closeEvent(QCloseEvent* event) override;  // persist scripts + variables on exit
    void resizeEvent(QResizeEvent* event) override;  // re-tile windows when tiled

public:
    void showJoinDialog();        // prompt for a channel and JOIN it
    void requestChannelList();    // /LIST on the active network, into the Channels List window
    void showAddressBook();       // favourite channels (add / remove / join)
    void showDccInfo();           // DCC status (transfers proxied through Lurker)
    void startDccChat();          // offer a DCC chat (proxied through Lurker)
    void sendFileTo(int net, const QString& target);  // pick a file, upload via Lurker, share the link
    bool ensureFileSend();                             // true if the backend can upload (else explains)
    void uploadFileTo(int net, const QString& target, const QString& path);  // upload a known path
    void shareImageTo(int net, const QString& target, const QImage& image);  // save+upload a pasted image
    void openLogsFolder();        // reveal the logging folder
    void showAbout();             // About box
    void promptLoadScript();      // file picker → loadScriptFile
    void promptLoadMtsTheme();     // file picker → applyMtsThemeFile
    void applyMtsThemeFile(const QString& path);  // parse + apply an .mts, refresh views
    void repaintWindowChrome();    // force a repaint of the menubar + MDI title bars after a theme change
    void pruneOrphanedWindowControls();  // hide stale maximized-MDI control widgets left in the menubar (Windows)
    void reRenderConversations();  // clear + re-fetch history so lines redraw in the new palette
    void importMircFolder(const QString& dir);  // import a mIRC folder's config/scripts
    // Adopts a backend (ownership transferred), wires it, and starts rendering.
    void useBackend(lurker::backend::ChatBackend* backend);
    // Open (creating if needed) and focus a conversation; also used by dev flags.
    void openConversation(int net, const QString& target);
    // Run an MSL line in the active (or status) window — used by // input and --msl.
    void runScript(const QString& line);
    void loadScriptFile(const QString& path, bool fireLoad = false);  // fireLoad: run `on LOAD` (interactive only)
    void unloadScriptFile(const QString& path);  // forget a script and reload the rest (mIRC /unload)
    void reloadScripts();        // clear + reload all tracked script files
    void openScriptEditor();     // Scripts Editor window
    QStringList scriptFiles() const { return m_scriptFiles; }
    QString scriptSummary() const;  // "N aliases, N events, N dialogs"
    // Dev: inject a synthetic incoming message (renders + fires on TEXT).
    void simulateMessage(int net, const QString& nick, const QString& target, const QString& text);
    void simulateListReply();  // dev/screenshot: feed synthetic RPL_LIST (321/322/323)
    void previewDemo();        // dev/screenshot: seed synthetic link/media previews
    void themeEventDemo();     // dev/screenshot: fire join/part/mode/… events (MTS templates)
    void previewDriftDemo();   // dev/screenshot: verify a late preview lands under its message
    void dccDemo();            // dev/screenshot: seed synthetic DCC transfers
    void urlDemo();            // dev/screenshot: seed synthetic caught URLs
    void channelCentralDemo(); // dev/screenshot: open Channel Central with synthetic state
    void cascadeDemo();        // dev/screenshot: open many buffers + cascade; buffer list must stay readable
    void reactionsDemo();      // dev/screenshot: a reply quote + reaction chips + a live reaction (#5)

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
        QStringList memberSigils; // parallel prefix sigils ("@","+",""), for access gating
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
    void refreshNicklists();  // re-apply stored members to every channel nicklist (after a theme load/clear)
    void onTopicChanged(int net, const QString& target, const QByteArray& topic);
    void onReadStateChanged(int net, const QString& target, int unread, int highlights);
    void updateBufferLabel(Buffer* buffer);
    bool bufferIsActive(const Buffer& buffer) const;
    void noteMessage(Buffer* buffer, long long id);  // track newest id + mark-read if active
    void setupScripting();
    void ensureSectionScripts(); // create+load the Aliases/Popups/Remotes/Users files
    void saveScriptList();       // persist the loaded-scripts list to config
    void loadPersistedScripts(); // reload the scripts that were loaded last session
    void saveVariables();        // persist %globals to config
    void loadVariables();        // restore %globals from config
    void setupToolbar();         // the top mIRC-style icon toolbar
    void updateMdiBackground();  // tint the MDI workspace to the active theme
    void applyTreebarStyle();    // user background colour/image + text colour for the networks tree
    Buffer* activeBuffer();
    void echoToWindow(int net, const QString& window, const QString& text);
    // Fire the MSL remote event (on TEXT/JOIN/…) for this IRC event; returns true
    // if a handler halted default processing (the caller then skips the built-in
    // display). Must run before rendering so /halt can suppress it (mIRC order).
    bool dispatchScriptEvent(const lurker::IrcEvent& event);
    void onInput(Buffer* buffer, const QString& text);
    void handleNickAction(int net, const QString& key, const QString& nick, const QString& action);
    void logLine(const Buffer* buffer, const QString& plainLine);  // append to the log file if enabled
    QString themeScope(int net, const QString& target) const;     // per-channel settings prefix
    void showChannelTheme(const QString& key);                    // per-channel theme override dialog
    // Script popup (menu nicklist) support: evaluate a caption and run a command
    // with $1 = the clicked nick and the channel as context.
    QString evalScriptText(const QString& text, const QString& nick, int net, const QString& chan);
    void runPopupCommand(const QString& command, const QString& nick, int net, const QString& chan);
    void rebuildScriptMenus();  // render `menu menubar` script items into the menu bar
    void populateScriptMenu(class QMenu* menu, const std::vector<lurker::msl::MenuItem>& items, const QString& nick);
    bool highlightMatch(int net, const QString& plainText) const;  // Options ▸ Highlight word/nick match
    void closeBuffer(Buffer* buffer);
    void activateBuffer(Buffer* buffer);  // restore, focus, and hydrate on first view
    void arrangeWindows(bool tile);       // show all buffers then cascade/tile them
    void maximizeWindows();               // mIRC default: active window fills the workspace
    void hydrateBuffer(Buffer* buffer);   // fetch backlog once

    Buffer* routeTarget(int net, const QString& target, ChatView::Kind fallback);
    // mIRC-style message routing: where a category (notice/whois/ctcp) lands,
    // per Options (Active window / Status / query). `def` is the fallback setting.
    Buffer* routeFor(const QString& category, const QString& def, int net);
    // Pinned/favourite channels (#26): persisted by network name + channel. In
    // the default per-network mode they float to the top of their network node;
    // with "tree/combinedPins" on they gather under one top-level Favourites node.
    bool isPinned(int net, const QString& chan) const;
    void setPinned(int net, const QString& chan, bool pin);
    void repositionPinned(Buffer* buf);
    QTreeWidgetItem* favouritesNode();     // the combined node, created on demand
    void pruneFavouritesNode();            // remove it once it holds nothing
    void reapplyPinPlacement();            // re-place every channel (on mode change)
    QString treeLabel(const Buffer& b) const;  // tree text (network prefix under Favourites)
    // Render an event via the active MTS theme's template (phase 2). Returns
    // true if a theme handled it, so the caller skips its built-in line.
    bool renderThemedEvent(Buffer* buf, const QString& key, const QHash<QString, QString>& vars,
                           const QString& ts);
    // Run a theme's `!script` body (#27) with the event's %:: vars set and echo
    // pointed at `buf`. Returns true (the event was handled by the theme script).
    bool dispatchThemeScript(Buffer* buf, const QHash<QString, QString>& vars, const QString& body);
    QString timestampFor(const std::string& isoTime) const;

    QMdiArea* m_mdi = nullptr;
    QTreeWidget* m_treebar = nullptr;
    class QDockWidget* m_treeDock = nullptr;  // the networks tree dock
    QToolBar* m_switchbar = nullptr;
    QToolBar* m_toolbar = nullptr;   // top icon toolbar
    QActionGroup* m_switchGroup = nullptr;
    QAction* m_connectAction = nullptr;
    QAction* m_disconnectAction = nullptr;
    QAction* m_tbDisconnect = nullptr;  // toolbar twin of m_disconnectAction

    lurker::backend::ChatBackend* m_backend = nullptr;      // == m_mux (the active connection set)
    lurker::backend::MultiplexBackend* m_mux = nullptr;     // owns all child connections
    QHash<QString, Buffer> m_buffers;              // key -> buffer
    QString m_activeKey;                            // last conversation the user activated ($active fallback)
    QStringList m_liveConnIds;                      // identities of open connections (double-connect guard)
    QHash<int, QTreeWidgetItem*> m_networkNodes;   // networkId -> tree node (server buffer item)
    QTreeWidgetItem* m_favouritesNode = nullptr;   // combined pins node (#26); null when unused
    QHash<int, QString> m_networkNames;            // networkId -> display name
    QHash<int, QString> m_networkNicks;            // networkId -> our nick (for $me)
    bool m_closing = false;
    bool m_windowsMaximized = true;  // MDI mode: active window fills the workspace (mIRC default)
    bool m_tiled = false;            // windows are tiled → re-tile when the main window resizes
    QList<QAction*> m_scriptMenuBarActions;  // top-level menus added by `menu menubar`
    class QMenu* m_helpMenu = nullptr;       // script menus insert before this
    class Updater* m_updater = nullptr;      // in-app GitHub updater
    class PreviewService* m_previews = nullptr;  // link/media previews via Lurker's proxy
    void configurePreviews();                    // (re)point the preview service at the backend

    GuiScriptHost m_scriptHost;
    std::unique_ptr<lurker::msl::Interpreter> m_msl;
    class ScriptSockets* m_sockets = nullptr;
    QStringList m_scriptFiles;  // loaded .mrc paths, for the editor + reload
    QString m_mtsScriptPath;    // the current MTS theme's companion .mrc (unloaded on theme change)
    QHash<QString, class ScriptDialog*> m_dialogs;  // open script dialogs by instance name
    // DCX (dcx.dll) emulation: dispatch its verbs, remember each marked dialog's
    // event-callback alias, and the last /dcxml load result ($dcxml(Loaded)).
    QString dcxCall(const QString& proc, const QString& data);
    QHash<QString, QString> m_dcxEventAlias;  // dialog name -> marked event alias
    bool m_dcxLastLoad = false;
    QHash<QString, class QTimer*> m_scriptTimers;   // /timer instances by name
    int m_timerSeq = 0;
    QHash<QString, class PictureWindow*> m_pictureWindows;  // /window -p canvases
    class PictureWindow* ensurePictureWindow(const QString& name);
    class ChannelListWindow* m_channelList = nullptr;  // /list results window
    int m_channelListNet = -1;                         // network the current list is for
    QTreeWidgetItem* m_channelListItem = nullptr;      // its treebar entry
    class ChannelListWindow* ensureChannelList(int net);
    class DccTransfersWindow* m_dccWindow = nullptr;   // DCC Transfers window
    QTreeWidgetItem* m_dccItem = nullptr;              // its treebar entry
    class DccTransfersWindow* ensureDccWindow(bool raise = true);
    QHash<QString, class ChannelCentralDialog*> m_channelCentrals;  // open Channel Central dialogs by buffer key
    void showChannelCentral(int net, const QString& channel);
    class AddressBookDialog* m_addressBook = nullptr;  // Address Book (channels/notify/ignore)
    QStringList m_ignoreMasks;                         // active ignore masks (from the Address Book)
    QStringList m_notifyList;                          // nicks being watched
    QSet<QString> m_notifyOnline;                      // which notify nicks are currently online
    class QTimer* m_notifyTimer = nullptr;             // ISON poll timer
    class QTimer* m_refitTimer = nullptr;              // debounces the maximized-MDI re-fit on resize (Windows)
    bool isIgnored(const lurker::IrcEvent& event) const;  // true if the source matches an ignore mask
    void pollNotify();                                 // (re)load the notify list and send an ISON
    class UrlCatcherWindow* m_urlCatcher = nullptr;    // URL Catcher window
    QTreeWidgetItem* m_urlCatcherItem = nullptr;       // its treebar entry
    QList<CaughtUrl> m_caughtUrls;                     // kept link history (newest first)
    class UrlCatcherWindow* ensureUrlCatcher(bool raise = true);
    void catchUrls(const QString& from, const QString& target, const QString& plainText);
    lurker::msl::ScriptTarget m_eventContext;  // active target while an on-event runs
    bool m_inEvent = false;
};

}  // namespace lurker::gui
