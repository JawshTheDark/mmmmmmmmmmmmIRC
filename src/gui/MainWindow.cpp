// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QBrush>
#include <QDateTime>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QFontComboBox>
#include <QComboBox>
#include <QColorDialog>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTime>
#include <QResizeEvent>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QTreeWidget>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "backend/MultiplexBackend.h"
#include "core/CommandParser.h"
#include "gui/AddressBookDialog.h"
#include "gui/ChannelCentralDialog.h"
#include "gui/ChannelListWindow.h"
#include "gui/DccTransfersWindow.h"
#include "gui/ServerListDialog.h"
#include "gui/PreviewService.h"
#include "gui/ConnectDialog.h"
#include "gui/FormattedText.h"
#include "gui/MtsTheme.h"
#include "gui/PictureWindow.h"
#include "gui/ScriptDialog.h"
#include "gui/OptionsDialog.h"
#include "gui/ScriptEditor.h"
#include "gui/ScriptSockets.h"
#include "gui/Theme.h"
#include "gui/ToolbarIcons.h"
#include "gui/Updater.h"

namespace lurker::gui {

using lurker::IrcEvent;
using lurker::IrcEventType;
using lurker::backend::ChatBackend;
using lurker::backend::DisplayMember;
using lurker::backend::NetworkInfo;

namespace {
ChatView::Kind kindFromInt(int k) {
    switch (static_cast<lurker::backend::BufferKind>(k)) {
        case lurker::backend::BufferKind::Channel: return ChatView::Kind::Channel;
        case lurker::backend::BufferKind::Dm: return ChatView::Kind::Query;
        case lurker::backend::BufferKind::Server: return ChatView::Kind::Status;
    }
    return ChatView::Kind::Channel;
}
bool looksLikeChannel(const QString& t) { return !t.isEmpty() && QStringLiteral("#&+!").contains(t.front()); }
// Lurker names each network's server log ":server:<networkId>"; collapse every
// such target to one per-network server buffer (which is the treebar node).
QString normTarget(const QString& t) {
    return t.startsWith(QStringLiteral(":server:")) ? QStringLiteral(":server:") : t;
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("mmmmmmmmmmmmIRC"));
    resize(1180, 760);

    m_previews = new PreviewService(this);  // created before any ChatView wants it

    m_mdi = new QMdiArea(this);
    setCentralWidget(m_mdi);
    updateMdiBackground();

    auto* treeDock = new QDockWidget(tr("Networks"), this);
    treeDock->setObjectName(QStringLiteral("treebar"));
    m_treeDock = treeDock;
    m_treebar = new QTreeWidget(treeDock);
    m_treebar->setHeaderHidden(true);
    // The buffer list must stay readable: never let a layout pass (e.g. Cascade/Tile
    // asking the MDI area for more room) squeeze the dock below a legible width.
    m_treebar->setMinimumWidth(140);
    treeDock->setWidget(m_treebar);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);
    // Single click switches to a window (mIRC behaviour); itemActivated also
    // covers keyboard (Enter) and double-click.
    auto treeOpen = [this](QTreeWidgetItem* item, int) {
        if (item && item == m_channelListItem && m_channelList) {
            if (auto* sub = qobject_cast<QMdiSubWindow*>(m_channelList->parentWidget())) {
                if (sub->isMinimized()) sub->showNormal();
                sub->show();
                m_mdi->setActiveSubWindow(sub);
            }
            return;
        }
        if (item && item == m_dccItem && m_dccWindow) {
            if (auto* sub = qobject_cast<QMdiSubWindow*>(m_dccWindow->parentWidget())) {
                if (sub->isMinimized()) sub->showNormal();
                sub->show();
                m_mdi->setActiveSubWindow(sub);
            }
            return;
        }
        if (item && item == m_urlCatcherItem && m_urlCatcher) {
            if (auto* sub = qobject_cast<QMdiSubWindow*>(m_urlCatcher->parentWidget())) {
                if (sub->isMinimized()) sub->showNormal();
                sub->show();
                m_mdi->setActiveSubWindow(sub);
            }
            return;
        }
        for (auto& b : m_buffers) {
            if (b.treeItem == item) {
                activateBuffer(&b);
                break;
            }
        }
    };
    connect(m_treebar, &QTreeWidget::itemClicked, this, treeOpen);
    connect(m_treebar, &QTreeWidget::itemActivated, this, treeOpen);
    m_treebar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_treebar, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QTreeWidgetItem* item = m_treebar->itemAt(pos);
        if (!item) return;
        Buffer* target = nullptr;
        for (auto& b : m_buffers) {
            if (b.treeItem == item) {
                target = &b;
                break;
            }
        }
        if (!target) {
            // A network parent node (not a buffer): connection-level actions so a
            // user can drop one network without disconnecting the others (#3).
            const int nodeNet = m_networkNodes.key(item, -2);
            if (nodeNet != -2 && m_mux) {
                QMenu menu(this);
                menu.addAction(tr("Disconnect this network"), this,
                               [this, nodeNet] { m_mux->disconnectNetwork(nodeNet); });
                menu.addAction(tr("Join channel..."), this, &MainWindow::showJoinDialog);
                menu.exec(m_treebar->mapToGlobal(pos));
            }
            return;
        }
        QMenu menu(this);
        const int net = target->networkId;
        if (target->kind == ChatView::Kind::Channel) {
            const QString chan = target->display;
            menu.addAction(tr("Activate"), this, [this, key = target->key] {
                if (Buffer* b = findBuffer(key)) activateBuffer(b);
            });
            menu.addAction(tr("Channel Central..."), this, [this, net, chan] { showChannelCentral(net, chan); });
            menu.addAction(tr("Part channel"), this, [this, net, chan] {
                if (m_backend && m_backend->isReady()) m_backend->partChannel(net, chan, QString());
            });
            menu.addAction(tr("Close"), this, [this, key = target->key] {
                if (Buffer* b = findBuffer(key)) closeBuffer(b);
            });
            menu.addSeparator();
            const bool pinned = isPinned(net, chan);
            menu.addAction(pinned ? tr("Unpin") : tr("Pin to top"), this, [this, net, chan, key = target->key] {
                setPinned(net, chan, !isPinned(net, chan));
                if (Buffer* b = findBuffer(key)) repositionPinned(b);
            });
            menu.addAction(tr("Channel theme..."), this, [this, key = target->key] { showChannelTheme(key); });
        } else if (target->kind == ChatView::Kind::Query) {
            menu.addAction(tr("Close query"), this, [this, key = target->key] {
                if (Buffer* b = findBuffer(key)) closeBuffer(b);
            });
        } else {  // Status / network node
            menu.addAction(tr("Disconnect"), this, [this] {
                if (m_backend) m_backend->disconnectAll();
            });
            menu.addAction(tr("Join channel..."), this, &MainWindow::showJoinDialog);
        }
        menu.exec(m_treebar->mapToGlobal(pos));
    });

    m_switchbar = new QToolBar(tr("Windows"), this);
    m_switchbar->setObjectName(QStringLiteral("switchbar"));
    addToolBar(Qt::BottomToolBarArea, m_switchbar);
    m_switchGroup = new QActionGroup(this);

    auto* file = menuBar()->addMenu(tr("&File"));
    m_connectAction = file->addAction(tr("&Connect..."), QKeySequence(QStringLiteral("Ctrl+N")), this,
                                      &MainWindow::showConnectDialog);
    file->addAction(tr("Server &List..."), this, &MainWindow::showServerList);
    m_disconnectAction = file->addAction(tr("&Disconnect"), this, [this] {
        if (m_backend) m_backend->disconnectAll();
    });
    m_disconnectAction->setEnabled(false);
    file->addSeparator();
    file->addAction(tr("&Send File..."), this, [this] {
        Buffer* b = activeBuffer();
        if (b && (b->kind == ChatView::Kind::Channel || b->kind == ChatView::Kind::Query))
            sendFileTo(b->networkId, b->display);
        else
            showDccInfo();  // no conversation focused — show the DCC manager instead
    });
    file->addAction(tr("&Load Script..."), this, &MainWindow::promptLoadScript);
    file->addAction(tr("&Import from mIRC..."), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Select your mIRC folder"));
        if (!dir.isEmpty()) importMircFolder(dir);
    });
    file->addAction(tr("&Scripts Editor..."), QKeySequence(QStringLiteral("Alt+R")), this,
                    &MainWindow::openScriptEditor);
    file->addAction(tr("&Options..."), QKeySequence(QStringLiteral("Alt+O")), this,
                    [this] { showOptions(); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("&Join Channel..."), QKeySequence(QStringLiteral("Ctrl+J")), this,
                         &MainWindow::showJoinDialog);
    toolsMenu->addAction(tr("Channels &List"), this, &MainWindow::requestChannelList);
    toolsMenu->addAction(tr("&Address Book..."), this, &MainWindow::showAddressBook);
    toolsMenu->addAction(tr("&URL Catcher"), this, [this] { ensureUrlCatcher(true); });
    toolsMenu->addSeparator();
    toolsMenu->addAction(tr("&DCC..."), this, &MainWindow::showDccInfo);
    toolsMenu->addAction(tr("Open Logs &Folder"), this, &MainWindow::openLogsFolder);

    auto* windowMenu = menuBar()->addMenu(tr("&Window"));
    windowMenu->addAction(tr("&Next Window"), QKeySequence(QStringLiteral("Ctrl+Tab")), this,
                          [this] { m_mdi->activateNextSubWindow(); });
    windowMenu->addAction(tr("&Previous Window"), QKeySequence(QStringLiteral("Ctrl+Shift+Tab")), this,
                          [this] { m_mdi->activatePreviousSubWindow(); });
    windowMenu->addSeparator();
    windowMenu->addAction(tr("&Cascade"), this, [this] { arrangeWindows(false); });
    windowMenu->addAction(tr("&Tile"), this, [this] { arrangeWindows(true); });
    windowMenu->addAction(tr("Ma&ximize"), this, &MainWindow::maximizeWindows);
    windowMenu->addAction(tr("Minimi&ze All"), this, [this] {
        for (auto& b : m_buffers)
            if (b.sub && b.sub->isVisible()) b.sub->showMinimized();
    });
    windowMenu->addSeparator();
    windowMenu->addAction(treeDock->toggleViewAction());

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto* themeGroup = new QActionGroup(this);
    QSettings settings;
    const QString current = settings.value(QStringLiteral("theme"), QStringLiteral("default")).toString();
    for (const Theme& t : availableThemes()) {
        QAction* a = themeMenu->addAction(t.name);
        a->setCheckable(true);
        a->setChecked(t.id == current);
        themeGroup->addAction(a);
        const QString id = t.id;
        connect(a, &QAction::triggered, this, [id] {
            applyTheme(*qApp, id);
            QSettings().setValue(QStringLiteral("theme"), id);
        });
    }
    themeMenu->addSeparator();
    connect(themeMenu->addAction(tr("Load MTS theme (.mts)…")), &QAction::triggered, this,
            [this] { promptLoadMtsTheme(); });
    connect(themeMenu->addAction(tr("Clear MTS theme")), &QAction::triggered, this, [this] {
        clearMtsTheme();
        if (!m_mtsScriptPath.isEmpty()) {  // unload the theme's companion script
            const QString s = m_mtsScriptPath;
            m_mtsScriptPath.clear();
            unloadScriptFile(s);
        }
        refreshColorSettings();
        applyTreebarStyle();
        for (auto& b : m_buffers)
            if (b.view) { b.view->applyDisplaySettings(); b.view->update(); }
        reRenderConversations();
        QSettings().remove(QStringLiteral("mts/path"));
        statusBar()->showMessage(tr("MTS theme cleared"), 4000);
    });
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu = helpMenu;
    helpMenu->addAction(tr("Check for &Updates..."), this, [this] {
        if (!m_updater) m_updater = new Updater(this);
        m_updater->checkForUpdates(false);
    });
    helpMenu->addAction(tr("&About mmmmmmmmmmmmIRC..."), this, &MainWindow::showAbout);

    connect(m_mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow* w) {
        if (m_closing) return;
        for (auto& b : m_buffers) {
            if (b.sub == w && b.switchAction) b.switchAction->setChecked(true);
        }
    });

    setupToolbar();
    viewMenu->addSeparator();
    viewMenu->addAction(m_toolbar->toggleViewAction());  // show/hide the toolbar

    ensureBuffer(-1, ChatView::Kind::Status, tr("Status"), statusKey());
    setupScripting();
    ensureSectionScripts();  // load the mIRC-style section files so user aliases persist
    loadPersistedScripts();  // reload the scripts loaded last session
    loadVariables();         // restore saved %globals
    m_ignoreMasks = AddressBookDialog::ignoreList();  // apply the saved ignore list
    applyTreebarStyle();                              // apply any saved networks-tree background
    // mIRC fires `on START` once the client is up and all scripts are loaded.
    {
        lurker::msl::Scope sc;
        m_msl->raiseEvent("start", std::string(), sc);
    }
    statusBar()->showMessage(tr("Not connected — File ▸ Connect"));

    // Quiet check for a newer release a few seconds after startup (opt-out).
    if (QSettings().value(QStringLiteral("other/checkUpdates"), true).toBool()) {
        QTimer::singleShot(4000, this, [this] {
            if (!m_updater) m_updater = new Updater(this);
            m_updater->checkForUpdates(true);
        });
    }
}

void MainWindow::setupToolbar() {
    // Idempotent: re-running it (e.g. after the icon style changes in Options)
    // rebuilds the buttons in place without stacking a second toolbar. Preserve
    // the Disconnect button's enabled state across a rebuild.
    const bool wasConnected = m_tbDisconnect && m_tbDisconnect->isEnabled();
    if (!m_toolbar) {
        m_toolbar = new QToolBar(tr("Toolbar"), this);
        m_toolbar->setObjectName(QStringLiteral("maintoolbar"));
        m_toolbar->setMovable(true);
        m_toolbar->setIconSize(QSize(24, 24));
        m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        addToolBar(Qt::TopToolBarArea, m_toolbar);
    } else {
        m_toolbar->clear();  // drops the old actions (m_tbDisconnect included)
    }

    const QColor fg = palette().color(QPalette::ButtonText);
    QColor accent = palette().color(QPalette::Highlight);
    if (!accent.isValid() || accent.lightness() > 220) accent = QColor(60, 120, 210);
    // Toolbar icons: flat Modern (palette-coloured) or glossy Classic 2000s.
    const auto iconStyle = QSettings().value(QStringLiteral("display/classicIcons"), false).toBool()
                               ? IconStyle::Classic
                               : IconStyle::Modern;

    auto add = [&](Glyph g, const QString& text, auto slot) {
        QAction* a = m_toolbar->addAction(toolbarIcon(g, fg, accent, iconStyle), text);
        a->setToolTip(text);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    add(Glyph::Connect, tr("Connect to a server"), &MainWindow::showConnectDialog);
    m_tbDisconnect = add(Glyph::Disconnect, tr("Disconnect"), [this] {
        if (m_backend) m_backend->disconnectAll();
    });
    m_tbDisconnect->setEnabled(wasConnected);
    m_toolbar->addSeparator();
    add(Glyph::Join, tr("Join a channel"), &MainWindow::showJoinDialog);
    add(Glyph::ChannelList, tr("Channels list"), &MainWindow::requestChannelList);
    add(Glyph::AddressBook, tr("Favourites / Address Book"), &MainWindow::showAddressBook);
    m_toolbar->addSeparator();
    add(Glyph::Options, tr("mIRC Options"), [this] { showOptions(); });
    add(Glyph::Colors, tr("Colors"), [this] { showOptions(QStringLiteral("Colors")); });
    m_toolbar->addSeparator();
    add(Glyph::ScriptEditor, tr("Script Editor"), &MainWindow::openScriptEditor);
    add(Glyph::LoadScript, tr("Load a script"), &MainWindow::promptLoadScript);
    m_toolbar->addSeparator();
    add(Glyph::DccSend, tr("DCC Send"), &MainWindow::showDccInfo);
    add(Glyph::DccChat, tr("DCC Chat"), &MainWindow::startDccChat);
    add(Glyph::Logs, tr("Open logs folder"), &MainWindow::openLogsFolder);
    m_toolbar->addSeparator();
    add(Glyph::About, tr("About mmmmmmmmmmmmIRC"), &MainWindow::showAbout);
}

void MainWindow::updateMdiBackground() {
    if (!m_mdi) return;
    // QMdiArea ignores stylesheet backgrounds, so tint it from the palette: a
    // touch darker than the window gives the classic "workspace" recess.
    m_mdi->setBackground(QBrush(palette().color(QPalette::Window).darker(130)));
}

void MainWindow::applyTreebarStyle() {
    if (!m_treebar) return;
    QSettings s;
    const QString bg = s.value(QStringLiteral("display/treebarBgColor")).toString();
    const QString img = s.value(QStringLiteral("display/treebarBgImage")).toString();
    const QString fg = s.value(QStringLiteral("display/treebarColor")).toString();
    QString css;
    if (!bg.isEmpty() || !img.isEmpty() || !fg.isEmpty()) {
        css = QStringLiteral("QTreeWidget{");
        if (!bg.isEmpty()) css += QStringLiteral("background-color:%1;").arg(bg);
        if (!img.isEmpty()) css += QStringLiteral("background-image:url(\"%1\");").arg(img);
        if (!fg.isEmpty()) css += QStringLiteral("color:%1;").arg(fg);
        css += QStringLiteral("}");
    }
    m_treebar->setStyleSheet(css);
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
        updateMdiBackground();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveVariables();   // persist %globals so they survive a restart/update
    saveScriptList();  // remember which scripts were loaded
    QMainWindow::closeEvent(event);
}

void MainWindow::saveScriptList() {
    // Persist user-loaded scripts (the auto-created section files reload on their
    // own, so we don't need to store them).
    QStringList user;
    const QStringList sections = ScriptEditor::sectionFiles();
    for (const QString& f : m_scriptFiles)
        if (!sections.contains(f)) user << f;
    QSettings().setValue(QStringLiteral("scripts/files"), user);
}

void MainWindow::loadPersistedScripts() {
    const QStringList files = QSettings().value(QStringLiteral("scripts/files")).toStringList();
    for (const QString& f : files)
        if (!m_scriptFiles.contains(f) && QFile::exists(f)) loadScriptFile(f);
}

void MainWindow::saveVariables() {
    if (!m_msl) return;
    QSettings s;
    s.beginGroup(QStringLiteral("vars"));
    s.remove(QString());  // clear previous
    s.beginWriteArray(QStringLiteral("v"));
    int i = 0;
    for (const auto& [name, value] : m_msl->globals()) {
        s.setArrayIndex(i++);
        s.setValue(QStringLiteral("n"), QString::fromStdString(name));
        s.setValue(QStringLiteral("d"), QString::fromStdString(value));
    }
    s.endArray();
    s.endGroup();
}

namespace {
// Parse a mIRC-style .ini into section → ordered [key,value] pairs. mIRC uses
// numbered keys (n0, n1, …) whose file order matters, so we keep insertion order
// rather than using QSettings (which sorts and mangles).
QHash<QString, QList<QPair<QString, QString>>> parseMircIni(const QString& path) {
    QHash<QString, QList<QPair<QString, QString>>> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QString section;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine());
        while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
        const QString t = line.trimmed();
        if (t.startsWith('[') && t.endsWith(']')) {
            section = t.mid(1, t.size() - 2).toLower();
        } else if (!section.isEmpty()) {
            const int eq = line.indexOf('=');
            if (eq > 0) out[section].append({line.left(eq).trimmed(), line.mid(eq + 1)});
        }
    }
    return out;
}

// A mIRC server spec: "...SERVER:host:[+]port[:]GROUP:net" → host, port, ssl.
void parseMircServer(const QString& spec, QString& host, int& port, bool& ssl) {
    const int si = spec.indexOf(QStringLiteral("SERVER:"));
    if (si < 0) return;
    QString rest = spec.mid(si + 7);
    const int gi = rest.indexOf(QStringLiteral("GROUP"));
    if (gi >= 0) rest = rest.left(gi);
    while (rest.endsWith(':')) rest.chop(1);
    const QStringList parts = rest.split(':');
    if (!parts.isEmpty()) host = parts[0];
    if (parts.size() >= 2) {
        QString p = parts[1];
        ssl = p.startsWith('+');
        if (ssl) p.remove(0, 1);
        port = p.toInt();
    }
}
}  // namespace

void MainWindow::importMircFolder(const QString& dir) {
    QDir d(dir);
    const auto mircIni = parseMircIni(d.filePath(QStringLiteral("mirc.ini")));
    QSettings s;
    int nVars = 0, nChans = 0, nAliases = 0, nServers = 0;
    QString nick;

    // Identity + current server from [mirc].
    for (const auto& kv : mircIni.value(QStringLiteral("mirc"))) {
        if (kv.first == QLatin1String("nick") && !kv.second.isEmpty()) {
            nick = kv.second;
            s.setValue(QStringLiteral("connect/nick"), kv.second);
        } else if (kv.first == QLatin1String("anick") && !kv.second.isEmpty()) {
            s.setValue(QStringLiteral("connect/altnick"), kv.second);
        } else if (kv.first == QLatin1String("email") && !kv.second.isEmpty()) {
            s.setValue(QStringLiteral("connect/email"), kv.second);
        } else if (kv.first == QLatin1String("user") && !kv.second.isEmpty()) {
            s.setValue(QStringLiteral("connect/name"), kv.second);
        } else if (kv.first == QLatin1String("host") && kv.second.contains(QStringLiteral("SERVER:"))) {
            QString h;
            int p = 6667;
            bool ssl = false;
            parseMircServer(kv.second, h, p, ssl);
            if (!h.isEmpty()) {
                s.setValue(QStringLiteral("connect/host"), h);
                s.setValue(QStringLiteral("connect/port"), p);
                s.setValue(QStringLiteral("connect/ssl"), ssl);
            }
        }
    }

    // Variables (vars.ini can live in the folder or its scripts/ subdir): "%name value".
    QString varsPath = d.filePath(QStringLiteral("scripts/vars.ini"));
    if (!QFile::exists(varsPath)) varsPath = d.filePath(QStringLiteral("vars.ini"));
    if (m_msl) {
        lurker::msl::Scope scope;
        for (const auto& kv : parseMircIni(varsPath).value(QStringLiteral("variables"))) {
            QString v = kv.second.trimmed();
            if (!v.startsWith('%')) continue;
            v.remove(0, 1);
            const int sp = v.indexOf(' ');
            const QString name = sp < 0 ? v : v.left(sp);
            const QString value = sp < 0 ? QString() : v.mid(sp + 1);
            if (name.isEmpty()) continue;
            m_msl->setVariable(name.toStdString(), value.toStdString(), false, scope);
            ++nVars;
        }
        saveVariables();
    }

    // Favourite channels from channels.ini: "#chan,,,"nets"".
    QStringList fav = s.value(QStringLiteral("favourites/channels")).toStringList();
    for (const auto& kv : parseMircIni(d.filePath(QStringLiteral("channels.ini"))).value(QStringLiteral("channels"))) {
        const QString chan = kv.second.section(',', 0, 0).trimmed();
        if (chan.startsWith('#') && !fav.contains(chan, Qt::CaseInsensitive)) {
            fav << chan;
            ++nChans;
        }
    }
    s.setValue(QStringLiteral("favourites/channels"), fav);

    // Aliases from aliases.ini: "/name /body" → our "alias name { body }".
    QString mrc;
    for (const auto& kv : parseMircIni(d.filePath(QStringLiteral("scripts/aliases.ini"))).value(QStringLiteral("aliases"))) {
        QString line = kv.second.trimmed();
        if (!line.startsWith('/')) continue;
        line.remove(0, 1);
        const int sp = line.indexOf(' ');
        if (sp <= 0) continue;
        const QString name = line.left(sp);
        QString body = line.mid(sp + 1).trimmed();
        if (body.startsWith('/')) body.remove(0, 1);
        if (name.isEmpty() || body.isEmpty()) continue;
        mrc += QStringLiteral("alias %1 { %2 }\n").arg(name, body);
        ++nAliases;
    }
    if (!mrc.isEmpty()) {
        const QString outDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/scripts");
        QDir().mkpath(outDir);
        const QString outPath = outDir + QStringLiteral("/imported-mirc.mrc");
        QFile of(outPath);
        if (of.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            of.write(mrc.toUtf8());
            of.close();
            loadScriptFile(outPath);
        }
    }

    // Servers from servers.ini → a stored list (for the connect dialog later).
    QStringList srv;
    for (const auto& kv : parseMircIni(d.filePath(QStringLiteral("servers.ini"))).value(QStringLiteral("servers"))) {
        QString h;
        int p = 6667;
        bool ssl = false;
        parseMircServer(kv.second, h, p, ssl);
        if (!h.isEmpty()) {
            srv << QStringLiteral("%1:%2%3").arg(h).arg(p).arg(ssl ? QStringLiteral(" (SSL)") : QString());
            ++nServers;
        }
    }
    if (!srv.isEmpty()) s.setValue(QStringLiteral("servers/imported"), srv);

    const QString msg = tr("Imported from mIRC:\n• %1 variables\n• %2 favourite channels\n• %3 aliases\n• %4 servers%5")
                            .arg(nVars)
                            .arg(nChans)
                            .arg(nAliases)
                            .arg(nServers)
                            .arg(nick.isEmpty() ? QString() : tr("\n• identity: %1").arg(nick));
    onStatusText(msg);
    QMessageBox::information(this, tr("Import from mIRC"), msg);
}

void MainWindow::loadVariables() {
    if (!m_msl) return;
    QSettings s;
    s.beginGroup(QStringLiteral("vars"));
    const int count = s.beginReadArray(QStringLiteral("v"));
    lurker::msl::Scope scope;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        const QString n = s.value(QStringLiteral("n")).toString();
        if (n.isEmpty()) continue;
        m_msl->setVariable(n.toStdString(), s.value(QStringLiteral("d")).toString().toStdString(), false, scope);
    }
    s.endArray();
    s.endGroup();
}

void MainWindow::showOptions(const QString& page) {
    OptionsDialog dlg(this);
    if (!page.isEmpty()) dlg.selectPage(page);
    if (dlg.exec() == QDialog::Accepted) {
        refreshColorSettings();  // pick up Colors-page edits for new messages
        configurePreviews();     // the inline-previews toggle may have changed
        applyTreebarStyle();     // networks-tree background/colour
        setupToolbar();          // the toolbar icon style may have changed
        reapplyPinPlacement();   // the combined-favourites toggle may have changed
        // Push display changes (font, scrollback) to every open conversation.
        for (auto& b : m_buffers) {
            if (b.view) b.view->applyDisplaySettings();
        }
    }
}

void MainWindow::promptLoadMtsTheme() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load MTS theme"), QString(),
                                                      tr("MTS themes (*.mts);;All files (*)"));
    if (!path.isEmpty()) applyMtsThemeFile(path);
}

void MainWindow::applyMtsThemeFile(const QString& path) {
    MtsTheme theme = MtsTheme::parse(path);
    if (!theme.apply()) {
        statusBar()->showMessage(tr("Could not load MTS theme: %1").arg(QFileInfo(path).fileName()), 5000);
        return;
    }
    QSettings().setValue(QStringLiteral("mts/path"), path);
    // Swap the companion script: unload the previous theme's .mrc (if any) and
    // load this theme's (SCRIPT directive, resolved next to the .mts) so themes
    // don't accumulate scripts. Reload only when the companion actually changed.
    const QString oldScript = m_mtsScriptPath;
    m_mtsScriptPath.clear();
    if (!theme.script.isEmpty()) {
        const QString sp = QFileInfo(QFileInfo(path).dir().filePath(theme.script)).absoluteFilePath();
        if (QFile::exists(sp)) m_mtsScriptPath = sp;
    }
    if (oldScript != m_mtsScriptPath) {
        if (!oldScript.isEmpty()) m_scriptFiles.removeAll(oldScript);
        if (!m_mtsScriptPath.isEmpty() && !m_scriptFiles.contains(m_mtsScriptPath))
            m_scriptFiles << m_mtsScriptPath;
        reloadScripts();
        saveScriptList();
        rebuildScriptMenus();
    }
    // Push the new palette to every open conversation and the chrome: the tree
    // (treebar), the MDI backdrop, and each view's background/font/nicklist.
    refreshColorSettings();
    applyTreebarStyle();
    configurePreviews();
    m_mdi->setBackground(QBrush(palette().color(QPalette::Window).darker(
        palette().color(QPalette::Window).lightness() < 128 ? 130 : 105)));
    for (auto& b : m_buffers)
        if (b.view) { b.view->applyDisplaySettings(); b.view->update(); }
    reRenderConversations();  // redraw history with the new palette (baked colours)
    statusBar()->showMessage(
        tr("Loaded MTS theme: %1").arg(theme.name.isEmpty() ? QFileInfo(path).fileName() : theme.name), 5000);
}

void MainWindow::reRenderConversations() {
    // The message document bakes resolved colours, so a palette change can't
    // re-colour existing lines — clear each conversation and re-fetch its history
    // so it redraws with the active theme (the visible one immediately, the rest
    // when next viewed). Status has no server-side history to refetch, so it keeps
    // its lines.
    for (auto& b : m_buffers) {
        if (b.view && b.kind != ChatView::Kind::Status) {
            b.view->clearMessages();
            b.hydrated = false;
        }
    }
    if (Buffer* a = activeBuffer()) hydrateBuffer(a);
}

bool MainWindow::renderThemedEvent(Buffer* buf, const QString& key, const QHash<QString, QString>& vars,
                                   const QString& ts) {
    const lurker::gui::MtsTheme* theme = lurker::gui::activeMtsTheme();
    if (!theme || !buf || !buf->view) return false;
    const QString line = theme->renderEvent(key, vars);
    if (line.isEmpty()) return false;
    // Prefer the theme's own timestamp (its format carries colour codes, so the
    // whole line renders through the formatter); otherwise keep our plain stamp.
    const QString mtsTs = theme->timestamp(QDateTime::currentDateTime());
    if (!mtsTs.isEmpty()) buf->view->addThemedLine(QString(), (mtsTs + QLatin1Char(' ') + line).toUtf8());
    else buf->view->addThemedLine(ts, line.toUtf8());
    return true;
}

void MainWindow::promptLoadScript() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load MSL script"), m_scriptHost.baseDir,
                                                      tr("mIRC scripts (*.mrc);;All files (*)"));
    if (!path.isEmpty()) loadScriptFile(path, /*fireLoad=*/true);
}

void MainWindow::showJoinDialog() {
    // Pick the network of the active window, else the first connected network.
    int net = -1;
    if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
    if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
    if (net < 0 || !m_backend || !m_backend->isReady()) {
        statusBar()->showMessage(tr("Connect to a server before joining a channel"), 6000);
        return;
    }
    bool ok = false;
    QString chan = QInputDialog::getText(this, tr("Join Channel"), tr("Channel (e.g. #lurker):"),
                                         QLineEdit::Normal, QStringLiteral("#"), &ok);
    chan = chan.trimmed();
    if (!ok || chan.isEmpty() || chan == QLatin1String("#")) return;
    if (!chan.startsWith(QLatin1Char('#')) && !chan.startsWith(QLatin1Char('&'))) chan.prepend(QLatin1Char('#'));
    m_backend->joinChannel(net, chan, QString());
    openConversation(net, chan);
}

lurker::gui::ChannelListWindow* MainWindow::ensureChannelList(int net) {
    m_channelListNet = net;
    if (!m_channelList) {
        m_channelList = new lurker::gui::ChannelListWindow;
        auto* sub = m_mdi->addSubWindow(m_channelList);
        sub->setWindowTitle(tr("Channels List"));
        sub->setAttribute(Qt::WA_DeleteOnClose);
        sub->resize(640, 460);
        sub->move(70, 50);
        connect(m_channelList, &lurker::gui::ChannelListWindow::joinRequested, this, [this](const QString& chan) {
            if (m_backend && m_backend->isReady() && m_channelListNet >= 0) {
                m_backend->joinChannel(m_channelListNet, chan, QString());
                openConversation(m_channelListNet, chan);
            }
        });
        connect(m_channelList, &QObject::destroyed, this, [this](QObject*) {
            if (m_closing) return;
            m_channelList = nullptr;
            delete m_channelListItem;  // drop its treebar entry
            m_channelListItem = nullptr;
        });
        // Give it its own top-level entry on the treebar (single-click to raise).
        m_channelListItem = new QTreeWidgetItem(m_treebar, {tr("Channels List")});
    }
    if (auto* sub = qobject_cast<QMdiSubWindow*>(m_channelList->parentWidget())) {
        if (sub->isMinimized()) sub->showNormal();
        sub->show();
        m_mdi->setActiveSubWindow(sub);
    }
    return m_channelList;
}

void MainWindow::channelCentralDemo() {
    showChannelCentral(1, QStringLiteral("#test"));
    if (ChannelCentralDialog* d = m_channelCentrals.value(keyFor(1, QStringLiteral("#test")))) {
        d->setTopicText(QStringLiteral("Welcome to #test — read the rules"));
        d->setCurrentModes(QStringLiteral("+ntm"), QString(), 0);
        d->setBanList({QStringLiteral("*!*@spammer.example"), QStringLiteral("baduser!*@*"),
                       QStringLiteral("*!*@*.blocked.net")});
    }
}

void MainWindow::showChannelCentral(int net, const QString& channel) {
    const QString key = keyFor(net, channel);
    if (ChannelCentralDialog* d = m_channelCentrals.value(key)) {
        d->raise();
        d->activateWindow();
        return;
    }
    auto* d = new ChannelCentralDialog(net, channel, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    // Seed the topic from what we already hold for this buffer.
    if (Buffer* b = findBuffer(key)) d->setTopicText(b->view ? b->view->currentTopic() : QString());
    connect(d, &ChannelCentralDialog::topicChangeRequested, this, [this](int n, const QString& ch, const QString& t) {
        if (m_backend && m_backend->isReady()) m_backend->setTopic(n, ch, t);
    });
    connect(d, &ChannelCentralDialog::modeChangeRequested, this, [this](int n, const QString& ch, const QString& spec) {
        if (m_backend && m_backend->isReady()) m_backend->sendRaw(n, QStringLiteral("MODE %1 %2").arg(ch, spec));
    });
    connect(d, &ChannelCentralDialog::stateRefreshRequested, this, [this](int n, const QString& ch) {
        if (m_backend && m_backend->isReady()) {
            m_backend->sendRaw(n, QStringLiteral("MODE %1").arg(ch));       // current modes → 324
            m_backend->sendRaw(n, QStringLiteral("MODE %1 +b").arg(ch));    // ban list → 367/368
        }
    });
    connect(d, &QObject::destroyed, this, [this, key](QObject*) {
        if (!m_closing) m_channelCentrals.remove(key);
    });
    m_channelCentrals.insert(key, d);
    d->show();
}

UrlCatcherWindow* MainWindow::ensureUrlCatcher(bool raise) {
    if (!m_urlCatcher) {
        m_urlCatcher = new UrlCatcherWindow;
        auto* sub = m_mdi->addSubWindow(m_urlCatcher);
        sub->setWindowTitle(tr("URL Catcher"));
        sub->setAttribute(Qt::WA_DeleteOnClose);
        sub->resize(640, 400);
        sub->move(110, 90);
        m_urlCatcher->setUrls(m_caughtUrls);  // show the history collected before it was opened
        connect(m_urlCatcher, &UrlCatcherWindow::cleared, this, [this] { m_caughtUrls.clear(); });
        connect(m_urlCatcher, &QObject::destroyed, this, [this](QObject*) {
            if (m_closing) return;
            m_urlCatcher = nullptr;
            delete m_urlCatcherItem;
            m_urlCatcherItem = nullptr;
        });
        m_urlCatcherItem = new QTreeWidgetItem(m_treebar, {tr("URL Catcher")});
    }
    if (raise) {
        if (auto* sub = qobject_cast<QMdiSubWindow*>(m_urlCatcher->parentWidget())) {
            if (sub->isMinimized()) sub->showNormal();
            sub->show();
            m_mdi->setActiveSubWindow(sub);
        }
    }
    return m_urlCatcher;
}

void MainWindow::catchUrls(const QString& from, const QString& target, const QString& plainText) {
    if (!QSettings().value(QStringLiteral("display/urlCatcher"), true).toBool()) return;
    static const QRegularExpression re(QStringLiteral(R"(https?://[^\s<>"')]+)"));
    auto it = re.globalMatch(plainText);
    const QString ts = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    while (it.hasNext()) {
        QString url = it.next().captured(0);
        while (!url.isEmpty() && QStringLiteral(".,;:!?]}'\"").contains(url.back())) url.chop(1);
        if (url.isEmpty()) continue;
        const CaughtUrl u{ts, from, target, url};
        m_caughtUrls.prepend(u);
        while (m_caughtUrls.size() > 500) m_caughtUrls.removeLast();  // cap the history
        if (m_urlCatcher) m_urlCatcher->addUrl(u);
    }
}

void MainWindow::urlDemo() {
    catchUrls(QStringLiteral("slakker"), QStringLiteral("#lurker"),
              QStringLiteral("check the repo https://github.com/JawshTheDark/mmmmmmmmmmmmIRC and the archive "
                             "https://archive.irc.so/"));
    catchUrls(QStringLiteral("jawsh"), QStringLiteral("#lurker"),
              QStringLiteral("docs at https://modern.ircdocs.horse/ and a clip https://example.com/clip.mp4"));
    catchUrls(QStringLiteral("friend"), QStringLiteral("#dev"),
              QStringLiteral("nice read: https://en.wikipedia.org/wiki/Internet_Relay_Chat"));
    ensureUrlCatcher(true);
}

void MainWindow::requestChannelList() {
    int net = -1;
    if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
    if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
    if (net < 0 || !m_backend || !m_backend->isReady()) {
        statusBar()->showMessage(tr("Connect to a server to list channels"), 6000);
        return;
    }
    ensureChannelList(net)->listBegin();  // show "loading..." right away
    m_backend->requestChannelList(net);
    statusBar()->showMessage(tr("Requested channel list..."), 6000);
}

void MainWindow::showAddressBook() {
    if (m_addressBook) {
        m_addressBook->raise();
        m_addressBook->activateWindow();
        return;
    }
    m_addressBook = new AddressBookDialog(this);
    m_addressBook->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_addressBook, &AddressBookDialog::joinRequested, this, [this](const QString& chan) {
        int net = -1;
        if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
        if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
        if (net >= 0 && m_backend && m_backend->isReady()) {
            m_backend->joinChannel(net, chan, QString());
            openConversation(net, chan);
        } else {
            statusBar()->showMessage(tr("Connect to a server first"), 6000);
        }
    });
    connect(m_addressBook, &AddressBookDialog::listsChanged, this, [this] {
        m_ignoreMasks = AddressBookDialog::ignoreList();  // apply edited ignore list immediately
        pollNotify();                                     // pick up notify-list edits
    });
    connect(m_addressBook, &QObject::destroyed, this, [this](QObject*) {
        if (!m_closing) m_addressBook = nullptr;
    });
    m_addressBook->setNotifyOnline(m_notifyOnline.values());
    m_addressBook->show();
}

bool MainWindow::isIgnored(const lurker::IrcEvent& event) const {
    if (m_ignoreMasks.isEmpty() || event.self) return false;
    const QString nick = QString::fromStdString(event.nick);
    const QString full = QStringLiteral("%1!%2@%3")
                             .arg(nick, QString::fromStdString(event.user), QString::fromStdString(event.host));
    for (const QString& mask : m_ignoreMasks) {
        // A mask without !/@ matches the nick alone; otherwise the full nick!user@host.
        const bool bare = !mask.contains(QLatin1Char('!')) && !mask.contains(QLatin1Char('@'));
        const QString target = bare ? nick : full;
        const QRegularExpression rx(QRegularExpression::wildcardToRegularExpression(mask),
                                    QRegularExpression::CaseInsensitiveOption);
        if (rx.match(target).hasMatch()) return true;
    }
    return false;
}

void MainWindow::pollNotify() {
    m_notifyList = AddressBookDialog::notifyList();
    if (!m_notifyTimer) {
        m_notifyTimer = new QTimer(this);
        m_notifyTimer->setInterval(30000);  // ISON poll every 30s
        connect(m_notifyTimer, &QTimer::timeout, this, &MainWindow::pollNotify);
    }
    if (m_notifyList.isEmpty()) {
        m_notifyTimer->stop();
        return;
    }
    if (!m_notifyTimer->isActive()) m_notifyTimer->start();
    if (m_backend && m_backend->isReady()) {
        int net = -1;
        if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
        if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
        if (net >= 0) m_backend->sendRaw(net, QStringLiteral("ISON %1").arg(m_notifyList.join(QLatin1Char(' '))));
    }
}

void MainWindow::startDccChat() {
    if (!m_backend || !m_backend->isReady()) {
        statusBar()->showMessage(tr("Connect to a server first"), 6000);
        return;
    }
    if (!m_backend->supportsDccChat()) {  // raw bouncer: explain the proxy model
        showDccInfo();
        return;
    }
    int net = -1;
    if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
    if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
    if (net < 0) {
        statusBar()->showMessage(tr("No connected network"), 6000);
        return;
    }
    bool ok = false;
    const QString nick =
        QInputDialog::getText(this, tr("DCC Chat"), tr("Nick to chat with:"), QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok || nick.isEmpty()) return;
    m_backend->startDccChat(net, nick);
    statusBar()->showMessage(tr("Offering DCC chat to %1 (proxied through Lurker)...").arg(nick), 6000);
}

void MainWindow::showDccInfo() {
    // On the raw bouncer there's no Lurker DCC API — explain the proxy model.
    if (m_backend && !m_backend->supportsDccTransfers()) {
        QSettings s;
        const QString dir = s.value(QStringLiteral("dcc/downloadDir"),
                                    QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                                .toString();
        QMessageBox::information(
            this, tr("DCC"),
            tr("<b>DCC transfers are proxied through Lurker.</b><br><br>"
               "Connect via the Lurker backend to use the Transfers window: incoming offers are "
               "accepted and pulled from Lurker, so transfers work behind NAT and from any device.<br><br>"
               "Download folder: <code>%1</code> (Options ▸ DCC).")
                .arg(dir.toHtmlEscaped()));
        return;
    }
    ensureDccWindow();                                 // open the Transfers window
    if (m_backend) m_backend->requestDccTransfers();   // refresh the list
}

bool MainWindow::ensureFileSend() {
    if (m_backend && m_backend->supportsFileSend()) return true;
    QMessageBox::information(this, tr("Send File"),
                            tr("Connect via the Lurker backend to send files (uploaded through "
                               "Lurker's file host, then shared as a link)."));
    return false;
}

void MainWindow::uploadFileTo(int net, const QString& target, const QString& path) {
    if (path.isEmpty() || !m_backend || !m_backend->supportsFileSend()) return;
    statusBar()->showMessage(tr("Uploading %1…").arg(QFileInfo(path).fileName()));
    m_backend->sendFile(net, target, path);
}

void MainWindow::shareImageTo(int net, const QString& target, const QImage& image) {
    if (image.isNull() || !ensureFileSend()) return;
    // A pasted/dragged bitmap has no backing file; persist it to a temp PNG that
    // outlives this call (sendFile streams it asynchronously), then upload that.
    const QString path =
        QDir(QDir::tempPath())
            .filePath(QStringLiteral("mmirc-paste-%1.png")
                          .arg(QDateTime::currentMSecsSinceEpoch()));
    if (!image.save(path, "PNG")) {
        statusBar()->showMessage(tr("Could not save the pasted image for upload"), 6000);
        return;
    }
    uploadFileTo(net, target, path);
}

void MainWindow::sendFileTo(int net, const QString& target) {
    if (!ensureFileSend()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Send file to %1").arg(target));
    uploadFileTo(net, target, path);
}

DccTransfersWindow* MainWindow::ensureDccWindow(bool raise) {
    if (!m_dccWindow) {
        m_dccWindow = new DccTransfersWindow;
        auto* sub = m_mdi->addSubWindow(m_dccWindow);
        sub->setWindowTitle(tr("DCC Transfers"));
        sub->setAttribute(Qt::WA_DeleteOnClose);
        sub->resize(720, 420);
        sub->move(90, 70);
        connect(m_dccWindow, &DccTransfersWindow::acceptRequested, this,
                [this](int id) { if (m_backend) m_backend->acceptDcc(id); });
        connect(m_dccWindow, &DccTransfersWindow::rejectRequested, this,
                [this](int id) { if (m_backend) m_backend->rejectDcc(id); });
        connect(m_dccWindow, &DccTransfersWindow::cancelRequested, this,
                [this](int id) { if (m_backend) m_backend->cancelDcc(id); });
        connect(m_dccWindow, &DccTransfersWindow::deleteRequested, this,
                [this](int id) { if (m_backend) m_backend->deleteDccFile(id); });
        connect(m_dccWindow, &DccTransfersWindow::saveRequested, this, [this](int id, const QString& name) {
            QSettings s;
            const QString dir = s.value(QStringLiteral("dcc/downloadDir"),
                                        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
                                    .toString();
            const QString suggested = QDir(dir).filePath(name.isEmpty() ? tr("download") : name);
            const QString path = QFileDialog::getSaveFileName(this, tr("Save received file"), suggested);
            if (!path.isEmpty() && m_backend) m_backend->saveDccFile(id, path);
        });
        connect(m_dccWindow, &DccTransfersWindow::sendRequested, this, [this] {
            if (!ensureFileSend()) return;
            // Choose the network (prompt only when there's more than one).
            const QList<int> nets = m_networkNames.keys();
            int net = nets.size() == 1 ? nets.first() : (activeBuffer() ? activeBuffer()->networkId : -1);
            if (nets.size() > 1) {
                QStringList names;
                for (int n : nets) names << m_networkNames.value(n);
                bool ok = false;
                const QString pick = QInputDialog::getItem(this, tr("Send File"), tr("Network:"), names, 0, false, &ok);
                if (!ok) return;
                net = nets.value(names.indexOf(pick), net);
            }
            bool ok = false;
            const QString nick =
                QInputDialog::getText(this, tr("Send File"), tr("Send to nick:"), QLineEdit::Normal, QString(), &ok)
                    .trimmed();
            if (!ok || nick.isEmpty()) return;
            const QString file = QFileDialog::getOpenFileName(this, tr("Send File to %1").arg(nick));
            if (!file.isEmpty()) uploadFileTo(net, nick, file);
        });
        connect(m_dccWindow, &QObject::destroyed, this, [this](QObject*) {
            if (m_closing) return;
            m_dccWindow = nullptr;
            delete m_dccItem;
            m_dccItem = nullptr;
        });
        m_dccItem = new QTreeWidgetItem(m_treebar, {tr("DCC Transfers")});
    }
    // Only steal focus when asked (explicit open, or a new incoming offer) — not
    // on every live progress update.
    if (raise) {
        if (auto* sub = qobject_cast<QMdiSubWindow*>(m_dccWindow->parentWidget())) {
            if (sub->isMinimized()) sub->showNormal();
            sub->show();
            m_mdi->setActiveSubWindow(sub);
        }
    }
    return m_dccWindow;
}

void MainWindow::openLogsFolder() {
    QSettings s;
    QString dir = s.value(QStringLiteral("irc/logDir")).toString();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    }
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::showAbout() {
    const QString version = QStringLiteral(APP_VERSION);
    QMessageBox::about(
        this, tr("About mmmmmmmmmmmmIRC"),
        tr("<h3>mmmmmmmmmmmmIRC %1</h3>"
           "<p>A clean-room, cross-platform IRC client — connect directly to any IRC network, "
           "with first-class Lurker support baked in — built for full mIRC feature and "
           "scripting (MSL) parity.</p>"
           "<p>Qt %2 · built %3</p>"
           "<p>Licensed under the Mozilla Public License 2.0. Not affiliated with or derived "
           "from mIRC; no mIRC code or artwork is used.</p>")
            .arg(version, QStringLiteral(QT_VERSION_STR), QStringLiteral(__DATE__)));
}

void MainWindow::handleNickAction(int net, const QString& key, const QString& nick, const QString& action) {
    // Query doesn't need an active connection — just open the window.
    if (action == QLatin1String("query")) {
        activateBuffer(&ensureBuffer(net, ChatView::Kind::Query, nick, keyFor(net, nick)));
        return;
    }
    if (action == QLatin1String("sendfile")) {
        sendFileTo(net, nick);
        return;
    }
    if (!m_backend || !m_backend->isReady()) {
        statusBar()->showMessage(tr("Not connected"), 5000);
        return;
    }
    Buffer* buf = findBuffer(key);
    const QString chan = buf ? buf->display : QString();
    if (action == QLatin1String("whois")) {
        m_backend->sendRaw(net, QStringLiteral("WHOIS %1").arg(nick));
    } else if (action == QLatin1String("slap")) {
        m_backend->sendAction(net, chan,
                              tr("slaps %1 around a bit with a large trout").arg(nick));
    } else if (action == QLatin1String("op")) {
        m_backend->sendRaw(net, QStringLiteral("MODE %1 +o %2").arg(chan, nick));
    } else if (action == QLatin1String("deop")) {
        m_backend->sendRaw(net, QStringLiteral("MODE %1 -o %2").arg(chan, nick));
    } else if (action == QLatin1String("voice")) {
        m_backend->sendRaw(net, QStringLiteral("MODE %1 +v %2").arg(chan, nick));
    } else if (action == QLatin1String("devoice")) {
        m_backend->sendRaw(net, QStringLiteral("MODE %1 -v %2").arg(chan, nick));
    } else if (action == QLatin1String("kick")) {
        m_backend->sendRaw(net, QStringLiteral("KICK %1 %2").arg(chan, nick));
    } else if (action == QLatin1String("ban")) {
        m_backend->sendRaw(net, QStringLiteral("MODE %1 +b %2!*@*").arg(chan, nick));
    } else if (action == QLatin1String("kickban")) {
        m_backend->sendRaw(net, QStringLiteral("MODE %1 +b %2!*@*").arg(chan, nick));
        m_backend->sendRaw(net, QStringLiteral("KICK %1 %2").arg(chan, nick));
    } else if (action.startsWith(QLatin1String("ctcp-"))) {
        m_backend->sendCtcp(net, nick, action.mid(5).toUpper(), QString());
    }
}

void MainWindow::logLine(const Buffer* buffer, const QString& plainLine) {
    if (!buffer) return;
    QSettings s;
    const bool isChannel = buffer->kind == ChatView::Kind::Channel;
    const bool isQuery = buffer->kind == ChatView::Kind::Query;
    if (isChannel && !s.value(QStringLiteral("irc/logChannels"), false).toBool()) return;
    if (isQuery && !s.value(QStringLiteral("irc/logQueries"), false).toBool()) return;
    if (!isChannel && !isQuery) return;  // never log the status window

    QString dir = s.value(QStringLiteral("irc/logDir")).toString();
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(dir);
    QString safe = buffer->display;
    safe.replace(QRegularExpression(QStringLiteral("[^\\w#&.+-]")), QStringLiteral("_"));
    const QString path = QStringLiteral("%1/net%2-%3.log").arg(dir, QString::number(buffer->networkId), safe);

    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QString out = plainLine;
    if (s.value(QStringLiteral("irc/logTimestamp"), true).toBool())
        out = QDateTime::currentDateTime().toString(QStringLiteral("[yyyy-MM-dd HH:mm:ss] ")) + out;
    f.write(out.toUtf8());
    f.write("\n");
}

bool MainWindow::highlightMatch(int net, const QString& plainText) const {
    QSettings s;
    if (!s.value(QStringLiteral("highlight/enabled"), true).toBool()) return false;
    const QString lower = plainText.toLower();
    if (s.value(QStringLiteral("highlight/onNick"), true).toBool()) {
        const QString me = m_networkNicks.value(net).toLower();
        if (!me.isEmpty() && lower.contains(me)) return true;
    }
    const QString words = s.value(QStringLiteral("highlight/words")).toString();
    const QStringList list = words.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    for (const QString& w : list) {
        if (lower.contains(w.toLower())) return true;
    }
    return false;
}

QString MainWindow::themeScope(int net, const QString& target) const {
    QString safe = target.toLower();
    safe.replace(QRegularExpression(QStringLiteral("[^a-z0-9]")), QStringLiteral("_"));
    return QStringLiteral("chan/%1/%2").arg(net).arg(safe);
}

void MainWindow::showChannelTheme(const QString& key) {
    Buffer* buf = findBuffer(key);
    if (!buf) return;
    const QString scope = buf->view->settingsScope();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Channel Theme — %1").arg(buf->display));
    dlg.resize(420, 220);
    auto* form = new QFormLayout(&dlg);
    form->addRow(new QLabel(tr("<i>Leave blank to inherit the global theme.</i>"), &dlg));

    QSettings s;
    auto* fontCombo = new QFontComboBox(&dlg);
    auto* fontOverride = new QCheckBox(tr("Override font"), &dlg);
    fontOverride->setChecked(s.contains(scope + QStringLiteral("/fontFamily")));
    if (fontOverride->isChecked()) fontCombo->setCurrentFont(QFont(s.value(scope + QStringLiteral("/fontFamily")).toString()));
    auto* size = new QSpinBox(&dlg);
    size->setRange(0, 48);
    size->setSpecialValueText(tr("(inherit)"));
    size->setValue(s.value(scope + QStringLiteral("/fontSize"), 0).toInt());
    auto* bgColor = new QLineEdit(s.value(scope + QStringLiteral("/bgColor")).toString(), &dlg);
    bgColor->setPlaceholderText(tr("inherit"));
    auto* pickColor = new QPushButton(tr("Pick..."), &dlg);
    auto* colorRow = new QHBoxLayout;
    colorRow->addWidget(bgColor, 1);
    colorRow->addWidget(pickColor);
    auto* bgImage = new QLineEdit(s.value(scope + QStringLiteral("/bgImage")).toString(), &dlg);
    bgImage->setPlaceholderText(tr("inherit"));
    auto* browse = new QPushButton(tr("Browse..."), &dlg);
    auto* imgRow = new QHBoxLayout;
    imgRow->addWidget(bgImage, 1);
    imgRow->addWidget(browse);

    form->addRow(fontOverride, fontCombo);
    form->addRow(tr("Font size"), size);
    form->addRow(tr("Background colour"), colorRow);
    form->addRow(tr("Background image"), imgRow);
    connect(pickColor, &QPushButton::clicked, &dlg, [bgColor] {
        const QColor c = QColorDialog::getColor(QColor(bgColor->text()), bgColor, tr("Background colour"));
        if (c.isValid()) bgColor->setText(c.name());
    });
    connect(browse, &QPushButton::clicked, &dlg, [bgImage] {
        const QString p = QFileDialog::getOpenFileName(bgImage, tr("Background image"), bgImage->text(),
                                                       tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
        if (!p.isEmpty()) bgImage->setText(p);
    });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    // Persist as overrides; clearing a field removes the override (inherits global).
    auto setOrClear = [&](const QString& suffix, const QString& value) {
        if (value.isEmpty()) s.remove(scope + QLatin1Char('/') + suffix);
        else s.setValue(scope + QLatin1Char('/') + suffix, value);
    };
    setOrClear(QStringLiteral("fontFamily"), fontOverride->isChecked() ? fontCombo->currentFont().family() : QString());
    setOrClear(QStringLiteral("fontSize"), size->value() > 0 ? QString::number(size->value()) : QString());
    setOrClear(QStringLiteral("bgColor"), bgColor->text().trimmed());
    setOrClear(QStringLiteral("bgImage"), bgImage->text().trimmed());
    buf->view->applyDisplaySettings();
}

void MainWindow::setupScripting() {
    m_scriptHost.echoFn = [this](const lurker::msl::ScriptTarget& where, const QString& text) {
        echoToWindow(where.networkId, QString::fromStdString(where.window), text);
    };
    m_scriptHost.messageFn = [this](int net, const QString& target, const QString& text) {
        if (m_backend && m_backend->isReady()) m_backend->sendMessage(net, target, text);
    };
    m_scriptHost.actionFn = [this](int net, const QString& target, const QString& text) {
        if (m_backend && m_backend->isReady()) m_backend->sendAction(net, target, text);
    };
    m_scriptHost.rawFn = [this](int net, const QString& line) {
        if (m_backend && m_backend->isReady()) m_backend->sendRaw(net, line);
    };
    m_scriptHost.activeFn = [this]() -> lurker::msl::ScriptTarget {
        if (m_inEvent) return m_eventContext;  // during an on-event, the default target is the event's
        Buffer* b = activeBuffer();
        if (!b) return {};
        return {b->networkId, b->display.toStdString()};
    };
    // $active and /echo -a want the window the user is looking at, even mid-event.
    m_scriptHost.activeWindowFn = [this]() -> lurker::msl::ScriptTarget {
        Buffer* b = activeBuffer();
        if (!b) return {};
        return {b->networkId, b->display.toStdString()};
    };
    m_scriptHost.meFn = [this](int net) { return m_networkNicks.value(net); };
    m_scriptHost.channelNicksFn = [this](int net, const QString& chan) -> QStringList {
        Buffer* b = findBuffer(keyFor(net, chan));
        return b ? b->memberNicks : QStringList();
    };
    m_scriptHost.myChannelsFn = [this](int net) -> QStringList {
        QStringList chans;
        for (auto& b : m_buffers)
            if (b.networkId == net && b.kind == ChatView::Kind::Channel) chans << b.display;
        return chans;
    };
    m_scriptHost.isOnFn = [this](int net, const QString& chan, const QString& nick) -> bool {
        Buffer* b = findBuffer(keyFor(net, chan));
        if (!b) return false;
        for (const QString& n : b->memberNicks)
            if (n.compare(nick, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    m_scriptHost.networkNameFn = [this](int net) { return m_networkNames.value(net); };
    m_scriptHost.serverHostFn = [this](int) {
        return QSettings().value(QStringLiteral("connect/host")).toString();
    };
    m_scriptHost.scriptFilesFn = [this] { return m_scriptFiles; };
    m_scriptHost.windowNamesFn = [this] {
        QStringList w;
        for (auto& b : m_buffers) w << b.display;
        return w;
    };
    m_scriptHost.windowLineCountFn = [this](const QString& name) -> int {
        for (auto& b : m_buffers)
            if (b.display.compare(name, Qt::CaseInsensitive) == 0) return b.view->lineCount();
        return 0;
    };
    m_scriptHost.windowLineFn = [this](const QString& name, int n) -> QString {
        for (auto& b : m_buffers)
            if (b.display.compare(name, Qt::CaseInsensitive) == 0) return b.view->lineAt(n);
        return QString();
    };
    m_scriptHost.errorFn = [this](const QString& text) {
        if (Buffer* b = activeBuffer()) b->view->addSystemLine(QByteArray("* ") + text.toUtf8());
    };
    // /auser and /ruser edited the access list: write it back to the Users
    // section (header + entries) so it survives a restart, like mIRC's users.ini.
    m_scriptHost.usersChangedFn = [](const QString& serialized) {
        const QString path = ScriptEditor::sectionFiles().value(3);  // users.mrc
        if (path.isEmpty()) return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(QByteArray("; Users\n") + serialized.toUtf8());
    };
    // Scripts' relative file paths resolve under a per-user data directory.
    m_scriptHost.baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(m_scriptHost.baseDir);
    m_scriptHost.openDialogFn = [this](const QString& instance, const lurker::msl::DialogTable& table, bool modal) {
        if (ScriptDialog* existing = m_dialogs.value(instance)) {
            existing->raise();
            existing->activateWindow();
            return;
        }
        auto* dlg = new ScriptDialog(table, instance, [this, instance](const QString& devent, int id) {
            if (m_msl) m_msl->raiseDialogEvent(instance.toStdString(), devent.toStdString(), id);
        });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QObject::destroyed, this, [this, instance](QObject*) { m_dialogs.remove(instance); });
        m_dialogs.insert(instance, dlg);
        if (modal) dlg->open();
        else dlg->show();
    };
    m_scriptHost.closeDialogFn = [this](const QString& instance) {
        if (ScriptDialog* dlg = m_dialogs.value(instance)) dlg->close();
    };
    m_scriptHost.didCommandFn = [this](const QString& instance, int id, const QString& sw, const QString& text) {
        if (ScriptDialog* dlg = m_dialogs.value(instance)) dlg->applyDid(id, sw, text);
    };
    m_scriptHost.didQueryFn = [this](const QString& instance, int id, const QString& prop) -> QString {
        ScriptDialog* dlg = m_dialogs.value(instance);
        if (!dlg) return prop == QLatin1String("isopen") ? QStringLiteral("0") : QString();
        if (prop == QLatin1String("isopen")) return QStringLiteral("1");
        return dlg->query(id, prop);
    };
    // /dll + $dll: we never load native DLLs. Emulate mdx.dll — the ubiquitous
    // addon that builds ListView/TreeView dialog controls — so its scripts run.
    m_scriptHost.dllCallFn = [this](const QString& file, const QString& proc, const QString& data) -> QString {
        // Windows scripts build DLL paths with backslashes; normalise so the
        // filename is extracted on any platform (QFileInfo only splits on '/').
        const QString dll = QFileInfo(QString(file).replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName().toLower();
        if (dll == QLatin1String("dcx.dll") || dll == QLatin1String("dcx"))
            return dcxCall(proc, data);
        if (dll != QLatin1String("mdx.dll") && dll != QLatin1String("mdx")) return QString();
        const QString p = proc.toLower();
        if (p == QLatin1String("setcontrolmdx")) {
            // data: <dialog> <id> <controltype> <styles...> [> viewfile]
            QString d = data;
            const int gt = d.indexOf(QLatin1Char('>'));
            if (gt >= 0) d = d.left(gt);  // drop the "> [ views.mdx ]" icon-view redirect
            const QStringList t = d.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (t.size() >= 3) {
                if (ScriptDialog* dlg = m_dialogs.value(t[0])) dlg->applyMdxControl(t[1].toInt(), t[2], t.mid(3));
            }
        }
        // Other procs (SetMircVersion, MarkDialog, SetFont, SetDialog, SetIcon, …)
        // are cosmetic for us and accepted as no-ops so the script proceeds.
        return QString();
    };
    m_scriptHost.startTimerFn = [this](const QString& name, int repeat, double interval, const QString& cmd) -> QString {
        QString id = name.isEmpty() ? QStringLiteral("_t%1").arg(++m_timerSeq) : name;
        if (QTimer* old = m_scriptTimers.take(id)) old->deleteLater();
        auto* t = new QTimer(this);
        t->setInterval(static_cast<int>(interval * 1000.0 + 0.5));
        auto remaining = std::make_shared<int>(repeat);  // 0 = infinite
        connect(t, &QTimer::timeout, this, [this, id, cmd, remaining] {
            runScript(cmd);
            if (*remaining > 0 && --*remaining == 0) {
                if (QTimer* tt = m_scriptTimers.take(id)) tt->deleteLater();
            }
        });
        m_scriptTimers.insert(id, t);
        t->start();
        return id;
    };
    m_scriptHost.stopTimerFn = [this](const QString& name) {
        if (QTimer* t = m_scriptTimers.take(name)) t->deleteLater();
    };
    m_scriptHost.openWindowFn = [this](const QString& name, const QString& sw) {
        if (sw == QLatin1String("query")) {  // /query → a query buffer on the active network
            int net = activeBuffer() ? activeBuffer()->networkId : -1;
            if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
            activateBuffer(&ensureBuffer(net, ChatView::Kind::Query, name, keyFor(net, name)));
            return;
        }
        if (sw.contains('p')) {
            PictureWindow* pw = ensurePictureWindow(name);
            if (auto* sub = qobject_cast<QMdiSubWindow*>(pw->parentWidget())) {
                if (sub->isMinimized()) sub->showNormal();
            }
            return;
        }
        Buffer& b = ensureBuffer(-1, ChatView::Kind::Status, name, keyFor(-1, name));
        if (b.sub && b.sub->isMinimized()) b.sub->showNormal();
    };
    m_scriptHost.clearWindowFn = [this](const QString& name) {
        for (auto& b : m_buffers) {
            if (b.display.compare(name, Qt::CaseInsensitive) == 0) {
                b.view->clearMessages();
                return;
            }
        }
        if (Buffer* b = findBuffer(keyFor(-1, name))) b->view->clearMessages();
    };
    m_scriptHost.setBarVisibleFn = [this](const QString& bar, int mode) {
        QWidget* w = nullptr;
        if (bar == QLatin1String("switchbar")) w = m_switchbar;
        else if (bar == QLatin1String("toolbar")) w = m_toolbar;
        else if (bar == QLatin1String("treebar")) w = m_treeDock;
        else if (bar == QLatin1String("menubar")) w = menuBar();
        else if (bar == QLatin1String("nicklist")) {
            if (Buffer* b = activeBuffer()) b->view->setNicklistVisible(mode == 2 ? -1 : mode);
            return;
        }
        if (!w) return;
        const bool show = (mode == 2) ? !w->isVisible() : (mode == 1);
        w->setVisible(show);
    };
    m_scriptHost.drawOpFn = [this](const QString& win, const QString& op, const QStringList& args) {
        ensurePictureWindow(win)->drawOp(op, args);
    };
    m_sockets = new ScriptSockets(this);
    m_sockets->onEvent = [this](const QString& name, const QString& event) {
        if (!m_msl) return;
        lurker::msl::Scope s;
        s.event = event.toStdString();
        s.target = name.toStdString();
        m_msl->raiseEvent(event.toStdString(), name.toStdString(), s);
    };
    m_scriptHost.sockOpenFn = [this](const QString& n, const QString& h, int p, bool ssl) { m_sockets->open(n, h, p, ssl); };
    m_scriptHost.sockWriteFn = [this](const QString& n, const QString& d, bool nl) { m_sockets->writeData(n, d, nl); };
    m_scriptHost.sockCloseFn = [this](const QString& n) { m_sockets->close(n); };
    m_scriptHost.sockReadLineFn = [this](const QString& n, bool& ok) { return m_sockets->readLine(n, ok); };
    m_scriptHost.sockInfoFn = [this](const QString& n, const QString& p) { return m_sockets->info(n, p); };
    m_msl = std::make_unique<lurker::msl::Interpreter>(m_scriptHost);
}

MainWindow::Buffer* MainWindow::activeBuffer() {
    if (QMdiSubWindow* w = m_mdi->activeSubWindow()) {
        for (auto& b : m_buffers) {
            if (b.sub == w) return &b;
        }
    }
    return findBuffer(statusKey());
}

void MainWindow::echoToWindow(int net, const QString& window, const QString& text) {
    Buffer* buf = nullptr;
    if (window.isEmpty()) {
        buf = activeBuffer();
    } else if (window == QStringLiteral("@status") || window == QStringLiteral("-s")) {
        buf = findBuffer(statusKey());
    } else if (window.startsWith(QLatin1Char('@'))) {
        // Custom windows are global and created on first use.
        buf = &ensureBuffer(-1, ChatView::Kind::Status, window, keyFor(-1, window));
    } else {
        buf = findBuffer(keyFor(net, window));
    }
    if (!buf) buf = activeBuffer();
    if (buf) buf->view->addMessage(QString(), QString(), text.toUtf8());
}

void MainWindow::runScript(const QString& line) {
    if (!m_msl) return;
    lurker::msl::Scope scope;
    m_msl->execute(line.toStdString(), scope);
}

// DCX (dcx.dll) verb dispatcher. The dcx_tools.mrc aliases funnel everything
// here through /dll + $dll; phase 1 handles Version, Mark and /dcxml (loading an
// XML layout into the host dialog), accepting the control verbs as no-ops so
// scripts run while control manipulation/queries land in a later phase.
QString MainWindow::dcxCall(const QString& proc, const QString& data) {
    const QString p = proc.toLower();
    if (p == QLatin1String("version")) return QStringLiteral("DCX 2.1 (emulated)");
    if (p == QLatin1String("isunloadsafe")) return QStringLiteral("0");
    // /dcx Mark <dialog> <eventalias>
    if (p == QLatin1String("mark")) {
        const QStringList t = data.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (t.size() >= 2) m_dcxEventAlias.insert(t[0], t[1]);
        return QString();
    }
    // $dcxml(Loaded)
    if (p == QLatin1String("_dcxml")) {
        if (data.section(QLatin1Char(' '), 0, 0).compare(QLatin1String("loaded"), Qt::CaseInsensitive) == 0)
            return m_dcxLastLoad ? QStringLiteral("1") : QStringLiteral("0");
        return QString();
    }
    // /dcxml -dx <dialog> <xmlname> <file>
    if (p == QLatin1String("dcxml")) {
        QString d = data.trimmed();
        if (d.startsWith(QLatin1Char('-'))) d = d.section(QLatin1Char(' '), 1);  // drop the -dx switch
        const QString dlg = d.section(QLatin1Char(' '), 0, 0);
        const QString xmlName = d.section(QLatin1Char(' '), 1, 1);
        QString fileRest = d.section(QLatin1Char(' '), 2).trimmed();
        if (fileRest.startsWith(QLatin1Char('"')) && fileRest.endsWith(QLatin1Char('"')))
            fileRest = fileRest.mid(1, fileRest.size() - 2);
        m_dcxLastLoad = false;
        if (ScriptDialog* dlgw = m_dialogs.value(dlg)) m_dcxLastLoad = dlgw->loadDcxml(fileRest, xmlName);
        return QString();
    }
    // xdid / _xdid / xdialog / _xpop / … — accepted; manipulation lands later.
    return QString();
}

QString MainWindow::evalScriptText(const QString& text, const QString& nick, int net, const QString& chan) {
    if (!m_msl) return text;
    lurker::msl::Scope scope;
    scope.params.push_back(nick.toStdString());  // $1 = clicked nick
    scope.chan = chan.toStdString();
    scope.target = chan.toStdString();
    scope.nick = nick.toStdString();
    m_eventContext = {net, chan.toStdString()};
    m_inEvent = true;
    const QString out = QString::fromStdString(m_msl->evaluate(text.toStdString(), scope));
    m_inEvent = false;
    return out;
}

void MainWindow::populateScriptMenu(QMenu* menu, const std::vector<lurker::msl::MenuItem>& items, const QString& nick) {
    int net = -1;
    QString chan;
    if (Buffer* b = activeBuffer()) {
        net = b->networkId;
        chan = b->display;
    }
    for (const auto& it : items) {
        if (it.separator) {
            menu->addSeparator();
            continue;
        }
        const QString caption = evalScriptText(QString::fromStdString(it.caption), nick, net, chan);
        if (!it.children.empty()) {
            populateScriptMenu(menu->addMenu(caption), it.children, nick);
        } else {
            const QString cmd = QString::fromStdString(it.command);
            menu->addAction(caption, this, [this, cmd, nick] {
                int n = -1;
                QString c;
                if (Buffer* b = activeBuffer()) {
                    n = b->networkId;
                    c = b->display;
                }
                runPopupCommand(cmd, nick, n, c);
            });
        }
    }
}

void MainWindow::rebuildScriptMenus() {
    // Clear any previously-added script menus, then render `menu menubar` items.
    for (QAction* a : m_scriptMenuBarActions) {
        menuBar()->removeAction(a);
        a->deleteLater();
    }
    m_scriptMenuBarActions.clear();
    if (!m_msl) return;
    const auto* items = m_msl->popupMenu("menubar");
    if (!items || items->empty()) return;
    QAction* before = m_helpMenu ? m_helpMenu->menuAction() : nullptr;
    for (const auto& top : *items) {
        if (top.separator) continue;
        const QString caption = evalScriptText(QString::fromStdString(top.caption), QString(), -1, QString());
        QAction* act = nullptr;
        if (!top.children.empty()) {
            auto* m = new QMenu(caption, this);
            populateScriptMenu(m, top.children, QString());
            act = before ? menuBar()->insertMenu(before, m) : menuBar()->addMenu(m);
        } else {
            const QString cmd = QString::fromStdString(top.command);
            auto* a = new QAction(caption, this);
            connect(a, &QAction::triggered, this, [this, cmd] {
                int n = -1;
                QString c;
                if (Buffer* b = activeBuffer()) {
                    n = b->networkId;
                    c = b->display;
                }
                runPopupCommand(cmd, QString(), n, c);
            });
            if (before) menuBar()->insertAction(before, a);
            else menuBar()->addAction(a);
            act = a;
        }
        if (act) m_scriptMenuBarActions.append(act);
    }
}

void MainWindow::runPopupCommand(const QString& command, const QString& nick, int net, const QString& chan) {
    if (!m_msl || command.isEmpty()) return;
    lurker::msl::Scope scope;
    scope.params.push_back(nick.toStdString());  // $1 = clicked nick
    scope.chan = chan.toStdString();
    scope.target = chan.toStdString();
    scope.nick = nick.toStdString();
    m_eventContext = {net, chan.toStdString()};
    m_inEvent = true;
    m_msl->execute(command.toStdString(), scope);
    m_inEvent = false;
}

void MainWindow::loadScriptFile(const QString& path, bool fireLoad) {
    if (!m_msl) return;
    QFileInfo info(path);
    // A script's own folder becomes the base for its relative file paths (mIRC $mircdir).
    m_scriptHost.baseDir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QStringList toLoad;
    if (info.isDir()) {
        for (const QFileInfo& fi : QDir(path).entryInfoList({QStringLiteral("*.mrc")}, QDir::Files, QDir::Name))
            toLoad << fi.absoluteFilePath();
    } else {
        toLoad << info.absoluteFilePath();
    }
    for (const QString& file : toLoad) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) continue;
        m_msl->loadScript(QString::fromUtf8(f.readAll()).toStdString());
        if (!m_scriptFiles.contains(file)) m_scriptFiles << file;
    }
    rebuildScriptMenus();  // pick up any `menu menubar` definitions
    saveScriptList();      // remember it across restarts
    // `on LOAD` fires only on an interactive load (never on a restart reload), so
    // one-time setup in it doesn't reset the user's config each launch.
    if (fireLoad) {
        lurker::msl::Scope sc;
        m_msl->raiseEvent("load", std::string(), sc);
    }
    onStatusText(tr("Loaded %1 file(s) — %2").arg(toLoad.size()).arg(scriptSummary()));
}

QString MainWindow::scriptSummary() const {
    return m_msl ? tr("%1 aliases, %2 event handlers").arg(m_msl->aliasCount()).arg(m_msl->eventHandlerCount())
                 : QString();
}

void MainWindow::unloadScriptFile(const QString& path) {
    if (!m_msl) return;
    const QString file = QFileInfo(path).absoluteFilePath();
    if (m_scriptFiles.removeAll(file) > 0) {
        reloadScripts();  // clears the engine and reloads the remaining files
        saveScriptList();
        rebuildScriptMenus();
        onStatusText(tr("Unloaded %1 — %2").arg(QFileInfo(file).fileName(), scriptSummary()));
    }
}

void MainWindow::reloadScripts() {
    if (!m_msl) return;
    m_msl->clearScripts();
    const QStringList files = m_scriptFiles;
    for (const QString& file : files) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) continue;
        m_scriptHost.baseDir = QFileInfo(file).absolutePath();  // so $scriptdir tags are correct (#32)
        const QByteArray body = f.readAll();
        // The Users section is the access list, not MSL code: parse it as
        // `<levels>:<mask>` entries so /auser and $level see them (#30).
        if (QFileInfo(file).fileName().compare(QStringLiteral("users.mrc"), Qt::CaseInsensitive) == 0)
            m_msl->loadUsers(QString::fromUtf8(body).toStdString());
        else
            m_msl->loadScript(QString::fromUtf8(body).toStdString());
    }
    rebuildScriptMenus();
    onStatusText(tr("Reloaded scripts — %1").arg(scriptSummary()));
}

void MainWindow::openScriptEditor() {
    ensureSectionScripts();
    auto* ed = new ScriptEditor(
        this, [this] { return m_scriptFiles; }, [this] { reloadScripts(); }, [this] { return scriptSummary(); },
        [this](const QString& path) { loadScriptFile(path, /*fireLoad=*/true); },
        [this]() -> QString {
            if (!m_msl) return QString();
            QString out;
            for (const auto& [name, value] : m_msl->globals())
                out += QStringLiteral("%%1 %2\n").arg(QString::fromStdString(name), QString::fromStdString(value));
            return out;
        },
        [this](const QString& path) { unloadScriptFile(path); });
    ed->setAttribute(Qt::WA_DeleteOnClose);
    ed->show();
}

void MainWindow::ensureSectionScripts() {
    // mIRC-style section files (Aliases/Popups/Remotes/Users) live under the app
    // data dir; create them if missing and make sure they're loaded + tracked so
    // what the user types in those editor tabs works and persists across runs.
    QDir().mkpath(ScriptEditor::sectionDir());
    static const char* headers[] = {"; Aliases\n", "; Popups\n", "; Remote scripts (on events, aliases, dialogs)\n",
                                    "; Users\n"};
    // Default right-click menus, editable here or extendable via remote `menu`
    // blocks (both merge). $1 is the clicked nick; # is the channel.
    static const char* kDefaultPopups =
        "; Popups — mIRC-style right-click menus. Edit these, or add `menu <type> { }`\n"
        "; blocks in a remote script; both merge. $1 = clicked nick, # = channel.\n\n"
        "menu nicklist {\n"
        "  Query: query $1\n"
        "  Whois: whois $1\n"
        "  Slap: describe # slaps $1 around a bit with a large trout\n"
        "  -\n"
        "  Control\n"
        "  .Op: mode # +o $1\n"
        "  .Deop: mode # -o $1\n"
        "  .Voice: mode # +v $1\n"
        "  .Devoice: mode # -v $1\n"
        "  .-\n"
        "  .Kick: kick # $1\n"
        "  .Ban: ban # $1\n"
        "  .Kick + Ban: ban # $1 | kick # $1\n"
        "  CTCP\n"
        "  .Version: ctcp $1 version\n"
        "  .Ping: ctcp $1 ping\n"
        "  .Time: ctcp $1 time\n"
        "  .Client Info: ctcp $1 clientinfo\n"
        "}\n\n"
        "menu channel {\n"
        "  Part: part #\n"
        "  Clear: clear\n"
        "}\n\n"
        "menu query {\n"
        "  Whois: whois $1\n"
        "  Close: close\n"
        "}\n";
    const QStringList files = ScriptEditor::sectionFiles();
    bool loadedAny = false;
    for (int i = 0; i < files.size(); ++i) {
        if (!QFile::exists(files[i])) {
            QFile f(files[i]);
            if (f.open(QIODevice::WriteOnly)) f.write(i == 1 ? kDefaultPopups : headers[i]);
        } else if (i == 1) {
            // Seed the default menus into an existing Popups file that has none yet.
            QFile r(files[i]);
            QString content = r.open(QIODevice::ReadOnly) ? QString::fromUtf8(r.readAll()) : QString();
            r.close();
            if (!content.contains(QStringLiteral("menu "), Qt::CaseInsensitive)) {
                QFile w(files[i]);
                if (w.open(QIODevice::Append)) w.write(kDefaultPopups);
            }
        }
        if (!m_scriptFiles.contains(files[i])) {  // load quietly (no per-file status spam)
            QFile f(files[i]);
            if (f.open(QIODevice::ReadOnly) && m_msl) {
                m_scriptHost.baseDir = QFileInfo(files[i]).absolutePath();  // correct $scriptdir tag (#32)
                const QByteArray body = f.readAll();
                if (i == 3)  // the Users section → access list, not MSL code (#30)
                    m_msl->loadUsers(QString::fromUtf8(body).toStdString());
                else
                    m_msl->loadScript(QString::fromUtf8(body).toStdString());
                m_scriptFiles << files[i];
                loadedAny = true;
            }
        }
    }
    if (loadedAny) rebuildScriptMenus();
}

void MainWindow::simulateListReply() {
    auto fire = [this](int numeric, std::vector<std::string> params) {
        lurker::IrcEvent ev;
        ev.type = IrcEventType::ServerText;
        ev.networkId = 1;
        ev.numeric = numeric;
        ev.raw.command = std::to_string(numeric);
        ev.raw.params = std::move(params);
        onEvent(ev);
    };
    // Raw-IRC (bouncer) path: 322 carries parsed params (channel/users/topic).
    fire(321, {"me", "Channel", "Users  Name"});
    fire(322, {"me", "#lurker", "128", "Lurker development and support"});
    fire(322, {"me", "#chat", "57", "General chatter"});
    fire(322, {"me", "#help", "12", "Ask your questions here"});
    fire(322, {"me", "#offtopic", "33", "Anything goes"});
    // Lurker WS path (issue #2): the backend pages the server-side /LIST cache and
    // synthesises 322s as text only ("<chan> <users> <topic>", no raw params),
    // which MainWindow's fallback parser must split back out.
    auto fireText = [this](const QString& text) {
        lurker::IrcEvent ev;
        ev.type = IrcEventType::ServerText;
        ev.networkId = 1;
        ev.numeric = 322;
        ev.text = text.toStdString();
        onEvent(ev);
    };
    fireText(QStringLiteral("##cpp 1337 Modern C++ discussion"));
    fireText(QStringLiteral("#lurker-ws 404 Served from the chanlist cache"));
    fire(323, {"me", "End of /LIST"});
}

void MainWindow::simulateMessage(int net, const QString& nick, const QString& target, const QString& text) {
    lurker::IrcEvent ev;
    ev.type = IrcEventType::Message;
    ev.networkId = net;
    ev.nick = nick.toStdString();
    ev.user = "user";
    ev.host = "host";
    ev.target = target.toStdString();
    ev.text = text.toStdString();
    onEvent(ev);
}

void MainWindow::themeEventDemo() {
    const int net = 1;
    m_networkNames.insert(net, QStringLiteral("DemoNet"));
    m_networkNicks.insert(net, QStringLiteral("jawsh"));
    auto fire = [&](IrcEventType t, const QString& nick, const QString& text, const QString& arg, bool self) {
        lurker::IrcEvent e;
        e.type = t;
        e.networkId = net;
        e.nick = nick.toStdString();
        e.user = nick.toLower().toStdString();
        e.host = "host.local";
        e.target = "#test";
        e.text = text.toStdString();
        e.arg = arg.toStdString();
        e.self = self;
        onEvent(e);
    };
    fire(IrcEventType::Join, QStringLiteral("jawsh"), {}, {}, true);
    fire(IrcEventType::Join, QStringLiteral("alice"), {}, {}, false);
    fire(IrcEventType::Message, QStringLiteral("bob"), QStringLiteral("hey all, testing the theme"), {}, false);
    fire(IrcEventType::Action, QStringLiteral("alice"), QStringLiteral("waves at everyone"), {}, false);
    fire(IrcEventType::Mode, QStringLiteral("op"), QStringLiteral("+o alice"), {}, false);
    fire(IrcEventType::NickChange, QStringLiteral("carol"), {}, QStringLiteral("carolyn"), false);
    fire(IrcEventType::Topic, QStringLiteral("op"), QStringLiteral("Welcome to the themed test channel"), {}, false);
    fire(IrcEventType::Part, QStringLiteral("dave"), QStringLiteral("heading out"), {}, false);
    fire(IrcEventType::Quit, QStringLiteral("bob"), QStringLiteral("Ping timeout: 240 seconds"), {}, false);
    fire(IrcEventType::Kick, QStringLiteral("op"), QStringLiteral("no flooding please"), QStringLiteral("alice"), false);
    openConversation(net, QStringLiteral("#test"));
}

void MainWindow::previewDemo() {
    // Seed synthetic descriptors + images so previews resolve from cache (no
    // network), then inject messages that reference them. Dev/screenshot aid.
    const int net = 1;
    m_networkNames.insert(net, QStringLiteral("DemoNet"));
    m_networkNicks.insert(net, QStringLiteral("jawsh"));
    m_previews->configure(QStringLiteral("https://demo.invalid"), QStringLiteral("demo"), true);

    auto make = [](int w, int h, QColor a, QColor b, const QString& label, QColor text) {
        QImage im(w, h, QImage::Format_RGB32);
        QPainter p(&im);
        QLinearGradient g(0, 0, w, h);
        g.setColorAt(0, a);
        g.setColorAt(1, b);
        p.fillRect(im.rect(), g);
        p.setPen(text);
        QFont f = p.font();
        f.setPointSize(qMax(10, h / 8));
        f.setBold(true);
        p.setFont(f);
        p.drawText(im.rect(), Qt::AlignCenter, label);
        return im;
    };

    PreviewDescriptor img;
    img.url = QStringLiteral("https://demo.invalid/sunset.jpg");
    img.status = QStringLiteral("ok");
    img.kind = QStringLiteral("image");
    img.src = QStringLiteral("/media/img1");
    m_previews->seedForTest(
        img, make(640, 360, QColor(0xFF, 0x8C, 0x42), QColor(0x8E, 0x24, 0xAA), QStringLiteral("sunset.jpg"), Qt::white));

    PreviewDescriptor page;
    page.url = QStringLiteral("https://demo.invalid/release");
    page.status = QStringLiteral("ok");
    page.kind = QStringLiteral("page");
    page.title = QStringLiteral("Release v0.1.11 · JawshTheDark/mmmmmmmmmmmmIRC");
    page.description = QStringLiteral("mdx.dll ListView dialogs, hash load/save, dotted vars, live channel list.");
    page.siteName = QStringLiteral("GitHub");
    page.thumb = QStringLiteral("/media/og1");
    m_previews->seedForTest(page, make(600, 300, QColor(0xF5, 0xF5, 0xF5), QColor(0xDA, 0xDA, 0xE0),
                                       QStringLiteral("New Release v0.1.11"), QColor(0x22, 0x22, 0x22)));

    PreviewDescriptor bare;
    bare.url = QStringLiteral("https://demo.invalid/archive");
    bare.status = QStringLiteral("ok");
    bare.kind = QStringLiteral("page");
    bare.title = QStringLiteral("IRC Script Archive");
    bare.siteName = QStringLiteral("archive.irc.so");
    m_previews->seedForTest(bare, QImage());

    PreviewDescriptor vid;
    vid.url = QStringLiteral("https://demo.invalid/clip");
    vid.status = QStringLiteral("ok");
    vid.kind = QStringLiteral("video");
    vid.title = QStringLiteral("sample clip");
    vid.siteName = QStringLiteral("demo.invalid");
    vid.thumb = QStringLiteral("/poster/vid1");
    m_previews->seedForTest(
        vid, make(480, 270, QColor(0x20, 0x30, 0x50), QColor(0x10, 0x18, 0x28), QStringLiteral("video"), Qt::white));

    simulateMessage(net, QStringLiteral("jawsh"), QStringLiteral("#previews"),
                    QStringLiteral("shipped it https://demo.invalid/release"));
    simulateMessage(net, QStringLiteral("slakker"), QStringLiteral("#previews"),
                    QStringLiteral("the archive https://demo.invalid/archive"));
    simulateMessage(net, QStringLiteral("jawsh"), QStringLiteral("#previews"),
                    QStringLiteral("clip https://demo.invalid/clip"));
    simulateMessage(net, QStringLiteral("slakker"), QStringLiteral("#previews"),
                    QStringLiteral("nice shot https://demo.invalid/sunset.jpg"));
    openConversation(net, QStringLiteral("#previews"));
}

void MainWindow::previewDriftDemo() {
    // Reproduce the reported bug: a preview whose URL is in an early message but
    // which resolves only AFTER later lines have loaded must still land under its
    // own message, not at the bottom.
    const int net = 1;
    m_networkNames.insert(net, QStringLiteral("DemoNet"));
    m_networkNicks.insert(net, QStringLiteral("jawsh"));
    m_previews->configure(QStringLiteral("https://demo.invalid"), QStringLiteral("demo"), true);
    m_previews->setTestDelay(400);  // resolve after the following messages are in
    QImage im(480, 240, QImage::Format_RGB32);
    {
        QPainter p(&im);
        QLinearGradient g(0, 0, 480, 240);
        g.setColorAt(0, QColor(0x30, 0x40, 0x60));
        g.setColorAt(1, QColor(0x10, 0x18, 0x28));
        p.fillRect(im.rect(), g);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPointSize(26);
        f.setBold(true);
        p.setFont(f);
        p.drawText(im.rect(), Qt::AlignCenter, QStringLiteral("shared.png"));
    }
    PreviewDescriptor d;
    d.url = QStringLiteral("https://demo.invalid/shared.png");
    d.status = QStringLiteral("ok");
    d.kind = QStringLiteral("image");
    d.src = QStringLiteral("/media/s1");
    m_previews->seedForTest(d, im);
    simulateMessage(net, QStringLiteral("synmuffin"), QStringLiteral("#test"),
                    QStringLiteral("I added foldable networks! https://demo.invalid/shared.png"));
    simulateMessage(net, QStringLiteral("quark"), QStringLiteral("#test"), QStringLiteral("nice work"));
    simulateMessage(net, QStringLiteral("jawsh"), QStringLiteral("#test"), QStringLiteral("looks good"));
    simulateMessage(net, QStringLiteral("d3fc0n"), QStringLiteral("#test"),
                    QStringLiteral("I find Claude works well with SSH keys"));
    simulateMessage(net, QStringLiteral("quark"), QStringLiteral("#test"),
                    QStringLiteral("the last line, which arrives before the preview resolves"));
    openConversation(net, QStringLiteral("#test"));
}

void MainWindow::dccDemo() {
    DccTransfersWindow* w = ensureDccWindow();
    auto mk = [](int id, const QString& dir, const QString& peer, const QString& file, qlonglong size,
                 qlonglong recv, const QString& state) {
        lurker::backend::DccTransfer t;
        t.id = id;
        t.direction = dir;
        t.peer = peer;
        t.filename = file;
        t.size = size;
        t.received = recv;
        t.state = state;
        return t;
    };
    QList<lurker::backend::DccTransfer> list;
    list << mk(3, QStringLiteral("incoming"), QStringLiteral("friend"), QStringLiteral("mixtape.mp3"), 8388608, 0,
               QStringLiteral("pending_approval"));
    list << mk(2, QStringLiteral("incoming"), QStringLiteral("filebot"), QStringLiteral("ubuntu-24.04.iso"),
               5100273664LL, 2448000000LL, QStringLiteral("receiving"));
    list << mk(1, QStringLiteral("incoming"), QStringLiteral("slakker"), QStringLiteral("holiday_photos.zip"),
               52428800, 52428800, QStringLiteral("completed"));
    list << mk(4, QStringLiteral("outgoing"), QStringLiteral("alice"), QStringLiteral("screenshot.png"), 1048576,
               1048576, QStringLiteral("completed"));
    list << mk(5, QStringLiteral("incoming"), QStringLiteral("spammer"), QStringLiteral("totally_legit.exe"), 102400,
               0, QStringLiteral("rejected"));
    w->setTransfers(list);
}

void MainWindow::dispatchScriptEvent(const lurker::IrcEvent& event) {
    if (!m_msl) return;
    QString name;
    QString subject = QString::fromStdString(event.text);
    const QString target = QString::fromStdString(event.target);
    switch (event.type) {
        case IrcEventType::Message: name = QStringLiteral("TEXT"); break;
        case IrcEventType::Action: name = QStringLiteral("ACTION"); break;
        case IrcEventType::Notice: name = QStringLiteral("NOTICE"); break;
        case IrcEventType::Join: name = QStringLiteral("JOIN"); subject = target; break;
        case IrcEventType::Part: name = QStringLiteral("PART"); subject = target; break;
        case IrcEventType::Kick: name = QStringLiteral("KICK"); subject = target; break;
        case IrcEventType::Quit: name = QStringLiteral("QUIT"); break;
        case IrcEventType::NickChange: name = QStringLiteral("NICK"); subject = QString::fromStdString(event.arg); break;
        case IrcEventType::Mode: name = QStringLiteral("MODE"); subject = target; break;
        case IrcEventType::Topic: name = QStringLiteral("TOPIC"); subject = target; break;
        default: return;
    }
    if (event.self || !m_msl->hasEvent(name.toStdString())) return;

    lurker::msl::Scope scope;
    scope.nick = event.nick;
    if (event.type == IrcEventType::NickChange) scope.newnick = event.arg;
    scope.address = event.user.empty() ? event.nick : event.nick + "!" + event.user + "@" + event.host;
    const bool isChan = looksLikeChannel(target);
    scope.chan = isChan ? target.toStdString() : std::string();
    scope.target = isChan ? target.toStdString() : event.nick;
    for (const QString& w : QString::fromStdString(event.text).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        scope.params.push_back(w.toStdString());

    m_eventContext = {event.networkId, scope.target};
    m_inEvent = true;
    m_msl->raiseEvent(name.toStdString(), subject.toStdString(), scope);
    m_inEvent = false;
}

MainWindow::~MainWindow() {
    m_closing = true;
    if (m_backend) m_backend->disconnect(this);
}

QString MainWindow::keyFor(int net, const QString& target) const {
    return QString::number(net) + QLatin1Char('\x1f') + target.toLower();
}

MainWindow::Buffer* MainWindow::findBuffer(const QString& key) {
    const auto it = m_buffers.find(key);
    return it == m_buffers.end() ? nullptr : &it.value();
}

QTreeWidgetItem* MainWindow::networkNode(int net) {
    if (net < 0) return nullptr;
    if (auto* node = m_networkNodes.value(net)) return node;
    auto* node = new QTreeWidgetItem(m_treebar, {m_networkNames.value(net, tr("Network %1").arg(net))});
    node->setExpanded(true);
    m_networkNodes.insert(net, node);
    return node;
}

bool MainWindow::isPinned(int net, const QString& chan) const {
    return QSettings()
        .value(QStringLiteral("pins"))
        .toStringList()
        .contains(m_networkNames.value(net) + QLatin1Char('\t') + chan.toLower());
}

void MainWindow::setPinned(int net, const QString& chan, bool pin) {
    QSettings s;
    QStringList pins = s.value(QStringLiteral("pins")).toStringList();
    const QString key = m_networkNames.value(net) + QLatin1Char('\t') + chan.toLower();
    pins.removeAll(key);
    if (pin) pins.prepend(key);
    s.setValue(QStringLiteral("pins"), pins);
}

QTreeWidgetItem* MainWindow::favouritesNode() {
    if (!m_favouritesNode) {
        m_favouritesNode = new QTreeWidgetItem({QStringLiteral("★ Favourites")});
        QFont f = m_favouritesNode->font(0);
        f.setBold(true);
        m_favouritesNode->setFont(0, f);
        m_treebar->insertTopLevelItem(0, m_favouritesNode);  // sits above the networks
        m_favouritesNode->setExpanded(true);
    }
    return m_favouritesNode;
}

void MainWindow::pruneFavouritesNode() {
    if (m_favouritesNode && m_favouritesNode->childCount() == 0) {
        delete m_favouritesNode;  // top-level: deleting detaches it from the tree
        m_favouritesNode = nullptr;
    }
}

void MainWindow::repositionPinned(Buffer* buf) {
    if (!buf || !buf->treeItem || buf->kind != ChatView::Kind::Channel) return;
    QTreeWidgetItem* item = buf->treeItem;
    QTreeWidgetItem* home = networkNode(buf->networkId);
    if (!home) return;
    const bool pinned = isPinned(buf->networkId, buf->display);
    const bool combined = QSettings().value(QStringLiteral("tree/combinedPins"), false).toBool();

    // Detach from wherever it currently hangs (a node's child, or top-level).
    auto detach = [&] {
        if (QTreeWidgetItem* p = item->parent()) p->takeChild(p->indexOfChild(item));
        else m_treebar->takeTopLevelItem(m_treebar->indexOfTopLevelItem(item));
    };

    if (pinned && combined) {
        QTreeWidgetItem* fav = favouritesNode();
        detach();
        fav->insertChild(0, item);  // newest pin on top of the Favourites list
    } else if (pinned) {
        detach();
        home->insertChild(0, item);  // float to the top of its own network
    } else {
        detach();
        home->addChild(item);  // unpinned: back under its network, at the bottom
    }
    updateBufferLabel(buf);  // network prefix appears/disappears with the move
    pruneFavouritesNode();   // a move out of Favourites may have emptied it
}

MainWindow::Buffer& MainWindow::ensureBuffer(int net, ChatView::Kind kind, const QString& display, const QString& key) {
    if (Buffer* existing = findBuffer(key)) return *existing;

    Buffer b;
    b.kind = kind;
    b.networkId = net;
    b.display = display;
    b.key = key;
    b.view = new ChatView(kind);
    b.view->setPreviewService(m_previews);  // inline link/media previews (Lurker proxy)
    if (kind == ChatView::Kind::Channel || kind == ChatView::Kind::Query)
        b.view->setSettingsScope(themeScope(net, display));
    // Script-defined window menu (menu channel / query / status) on right-click.
    const QString menuType = kind == ChatView::Kind::Channel  ? QStringLiteral("channel")
                             : kind == ChatView::Kind::Query   ? QStringLiteral("query")
                                                               : QStringLiteral("status");
    b.view->setWindowPopupProvider([this, menuType](QMenu* menu) {
        if (!m_msl) return;
        const auto* items = m_msl->popupMenu(menuType.toStdString());
        if (!items || items->empty()) return;
        menu->addSeparator();
        populateScriptMenu(menu, *items, QString());
    });

    b.sub = m_mdi->addSubWindow(b.view);
    b.sub->setWindowTitle(display);
    b.sub->setAttribute(Qt::WA_DeleteOnClose);
    b.sub->resize(760, 480);
    // mIRC keeps one window filling the workspace and switches between them. The
    // first window shows (maximized if that's the current mode); later buffers are
    // created hidden so opening dozens of channels doesn't litter the MDI with
    // minimized stubs — activateBuffer shows one when its row/tab is selected.
    if (m_buffers.isEmpty()) {
        if (m_windowsMaximized) b.sub->showMaximized();
        else b.sub->show();
    } else {
        b.sub->hide();
    }

    // Tree placement: a network's server buffer IS its top-level node; channels
    // and queries hang under that node; the global status sits at the top.
    if (net >= 0 && kind == ChatView::Kind::Status) {
        b.treeItem = networkNode(net);  // the network node doubles as the server row
    } else if (net >= 0) {
        QTreeWidgetItem* parent = networkNode(net);
        b.treeItem = new QTreeWidgetItem(parent, {display});
        parent->setExpanded(true);
    } else {
        b.treeItem = new QTreeWidgetItem(m_treebar, {display});
    }
    if (kind == ChatView::Kind::Channel) repositionPinned(&b);  // pinned float to top (#26)

    b.switchAction = m_switchbar->addAction(display);
    b.switchAction->setCheckable(true);
    m_switchGroup->addAction(b.switchAction);

    if (kind == ChatView::Kind::Channel) {
        const QString key_ = key;
        b.view->setNickProvider([this, key_]() -> QStringList {
            Buffer* buf = findBuffer(key_);
            return buf ? buf->memberNicks : QStringList();
        });
        const QString chan_ = display;
        b.view->setNickPopupProvider([this, net, chan_](QMenu* menu, const QString& nick) {
            if (!m_msl) return;
            const auto* items = m_msl->popupMenu("nicklist");
            if (!items || items->empty()) return;
            std::function<void(QMenu*, const std::vector<lurker::msl::MenuItem>&)> build =
                [&](QMenu* m, const std::vector<lurker::msl::MenuItem>& list) {
                    for (const auto& it : list) {
                        if (it.separator) {
                            m->addSeparator();
                            continue;
                        }
                        const QString caption =
                            evalScriptText(QString::fromStdString(it.caption), nick, net, chan_);
                        if (!it.children.empty()) {
                            build(m->addMenu(caption), it.children);
                        } else {
                            const QString cmd = QString::fromStdString(it.command);
                            m->addAction(caption, this,
                                         [this, cmd, nick, net, chan_] { runPopupCommand(cmd, nick, net, chan_); });
                        }
                    }
                };
            build(menu, *items);
        });
    }

    const QString key_ = key;
    connect(b.switchAction, &QAction::triggered, this, [this, key_] {
        if (Buffer* buf = findBuffer(key_)) activateBuffer(buf);
    });
    connect(b.view, &ChatView::lineEntered, this, [this, key_](const QString& text) {
        if (Buffer* buf = findBuffer(key_)) onInput(buf, text);
    });
    connect(b.view, &ChatView::nickActivated, this, [this, net](const QString& nick) {
        activateBuffer(&ensureBuffer(net, ChatView::Kind::Query, nick, keyFor(net, nick)));
    });
    connect(b.view, &ChatView::nickContextAction, this, [this, net, key_](const QString& nick, const QString& action) {
        handleNickAction(net, key_, nick, action);
    });
    // Drag-drop / paste of files or an image → upload via Lurker, share the link here.
    connect(b.view, &ChatView::shareFiles, this, [this, key_](const QStringList& paths) {
        Buffer* buf = findBuffer(key_);
        if (!buf || paths.isEmpty() || !ensureFileSend()) return;
        for (const QString& p : paths) uploadFileTo(buf->networkId, buf->display, p);
    });
    connect(b.view, &ChatView::shareImage, this, [this, key_](const QImage& image) {
        if (Buffer* buf = findBuffer(key_)) shareImageTo(buf->networkId, buf->display, image);
    });
    connect(b.sub, &QObject::destroyed, this, [this, key_](QObject*) {
        if (m_closing) return;
        if (Buffer* buf = findBuffer(key_)) {
            if (buf->switchAction) buf->switchAction->deleteLater();
            // If this buffer's row is a network node, forget it so a later
            // buffer on that network rebuilds the node instead of dangling.
            const int nodeNet = m_networkNodes.key(buf->treeItem, -2);
            if (nodeNet != -2) m_networkNodes.remove(nodeNet);
            delete buf->treeItem;
            m_buffers.remove(key_);
            pruneFavouritesNode();  // a favourited channel may have been the last one
        }
    });

    return m_buffers.insert(key, b).value();
}

void MainWindow::useBackend(ChatBackend* backend) {
    // The GUI talks to a single MultiplexBackend; each connection is added as a
    // child so a Lurker connection and one or more direct-IRC networks can run at
    // the same time (issue #3). Signals are wired to the multiplex once.
    if (!m_mux) {
        m_mux = new lurker::backend::MultiplexBackend(this);
        m_backend = m_mux;
        connect(m_mux, &ChatBackend::statusText, this, &MainWindow::onStatusText);
        connect(m_mux, &ChatBackend::networkUpdated, this, &MainWindow::onNetworkUpdated);
        connect(m_mux, &ChatBackend::bufferOpened, this, &MainWindow::onBufferOpened);
        connect(m_mux, &ChatBackend::event, this, &MainWindow::onEvent);
        connect(m_mux, &ChatBackend::membersReplaced, this, &MainWindow::onMembersReplaced);
        connect(m_mux, &ChatBackend::topicChanged, this, &MainWindow::onTopicChanged);
        connect(m_mux, &ChatBackend::readStateChanged, this, &MainWindow::onReadStateChanged);
        connect(m_mux, &ChatBackend::reactionUpdated, this,
                [this](int net, const QString& target, qlonglong messageId, const QString& nick,
                       const QString& value, bool self, bool remove) {
                    if (Buffer* b = findBuffer(keyFor(net, target)))
                        b->view->applyReaction(messageId, nick, value, self, remove);
                });
        connect(m_mux, &ChatBackend::readyChanged, this, [this] {
            const bool ready = m_backend && m_backend->isReady();
            m_disconnectAction->setEnabled(ready);
            if (m_tbDisconnect) m_tbDisconnect->setEnabled(ready);
            statusBar()->showMessage(ready ? tr("Connected") : tr("Disconnected"));
            configurePreviews();  // the feature flag + token are known once ready
            if (ready) pollNotify();  // begin watching the notify list
        });
        connect(m_mux, &ChatBackend::closed, this, [this] { statusBar()->showMessage(tr("Disconnected")); });
        connect(m_mux, &ChatBackend::dccTransferList, this, [this](const QList<lurker::backend::DccTransfer>& list) {
            if (!list.isEmpty()) ensureDccWindow(false)->setTransfers(list);  // seed quietly on connect
        });
        connect(m_mux, &ChatBackend::dccTransferUpdated, this, [this](const lurker::backend::DccTransfer& t) {
            // Raise the window for a fresh incoming offer; stay quiet for progress ticks.
            ensureDccWindow(t.awaitingApproval() && t.direction != QLatin1String("outgoing"))->upsert(t);
        });
        connect(m_mux, &ChatBackend::dccFileSaved, this, [this](int, const QString& path) {
            statusBar()->showMessage(tr("Saved %1").arg(path), 8000);
        });
        connect(m_mux, &ChatBackend::fileUploadProgress, this, [this](const QString& name, int pct) {
            statusBar()->showMessage(tr("Uploading %1… %2%").arg(name).arg(pct));
        });
        connect(m_mux, &ChatBackend::fileUploaded, this,
                [this](int net, const QString& target, const QString& name, const QString& url) {
                    // Drop the shared link into the target's composer so the user confirms the
                    // send (and can add a caption) rather than it going out silently.
                    openConversation(net, target);
                    if (Buffer* b = findBuffer(keyFor(net, target))) b->view->setInputText(url);
                    statusBar()->showMessage(tr("Uploaded %1 — press Enter to send the link").arg(name), 8000);
                });
        connect(m_mux, &ChatBackend::fileUploadError, this, [this](const QString& name, const QString& err) {
            statusBar()->showMessage(tr("Upload of %1 failed: %2").arg(name, err), 10000);
        });
    }
    m_mux->addChild(backend);
    configurePreviews();
}

void MainWindow::configurePreviews() {
    if (!m_previews) return;
    const bool wanted = QSettings().value(QStringLiteral("display/inlinePreviews"), true).toBool();
    if (m_backend && m_backend->supportsPreviews())
        m_previews->configure(m_backend->previewBase(), m_backend->authToken(), wanted);
    else
        m_previews->setEnabled(false);
}

void MainWindow::closeBuffer(Buffer* buffer) {
    if (buffer && buffer->key != statusKey() && buffer->sub) buffer->sub->close();
}

PictureWindow* MainWindow::ensurePictureWindow(const QString& name) {
    if (PictureWindow* existing = m_pictureWindows.value(name)) return existing;
    auto* pw = new PictureWindow;
    auto* sub = m_mdi->addSubWindow(pw);
    sub->setWindowTitle(name);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    sub->resize(580, 400);
    sub->move(40, 40);
    sub->show();
    m_pictureWindows.insert(name, pw);
    connect(sub, &QObject::destroyed, this, [this, name](QObject*) {
        if (!m_closing) m_pictureWindows.remove(name);
    });
    return pw;
}

void MainWindow::reactionsDemo() {
    m_networkNames.insert(1, QStringLiteral("Lurker"));
    openConversation(1, QStringLiteral("#chat"));
    Buffer* b = findBuffer(keyFor(1, QStringLiteral("#chat")));
    if (!b) return;
    ChatView* v = b->view;
    const QString thumbsUp = QString::fromUtf8("\xF0\x9F\x91\x8D");
    const QString heart = QString::fromUtf8("\xE2\x9D\xA4");
    const QString party = QString::fromUtf8("\xF0\x9F\x8E\x89");
    v->addMessage(QStringLiteral("12:00"), QStringLiteral("synmuffin"),
                  QByteArray("has anyone tried the new build?"), false, false, false, 101);
    v->applyReaction(101, QStringLiteral("bob"), heart, true, false);  // ❤ on the question (ours)
    v->addMessage(QStringLiteral("12:01"), QStringLiteral("alice"), QByteArray("yeah, works great here"), false,
                  false, false, 102);
    // A reply to 101, with two 👍 reactions already standing on it.
    v->addMessage(QStringLiteral("12:02"), QStringLiteral("bob"), QByteArray("same — the previews are slick"), false,
                  false, false, 103, QStringLiteral("synmuffin"), QStringLiteral("has anyone tried the new build?"));
    v->applyReaction(103, QStringLiteral("alice"), thumbsUp, false, false);
    v->applyReaction(103, QStringLiteral("carol"), thumbsUp, false, false);
    activateBuffer(b);
    // A reaction arriving live after the lines were drawn.
    QTimer::singleShot(400, this, [this, party] {
        if (Buffer* buf = findBuffer(keyFor(1, QStringLiteral("#chat"))))
            buf->view->applyReaction(103, QStringLiteral("dave"), party, false, false);
    });
}

void MainWindow::openConversation(int net, const QString& target) {
    const ChatView::Kind kind = looksLikeChannel(target) ? ChatView::Kind::Channel : ChatView::Kind::Query;
    activateBuffer(&ensureBuffer(net, kind, target, keyFor(net, target)));
}

void MainWindow::cascadeDemo() {
    // dev/screenshot (issue #1): open several channels across two networks and
    // cascade them. Before the fix, Cascade squeezed the Networks dock until the
    // channel names were unreadable; now the treebar and nicklists keep a minimum.
    m_networkNames.insert(1, QStringLiteral("Libera.Chat"));
    m_networkNames.insert(2, QStringLiteral("Rizon"));
    const struct { int net; const char* chan; } chans[] = {
        {1, "#mmmmmmmmmmmmIRC"}, {1, "#linux"}, {1, "#qt-development"}, {1, "##c++"},
        {2, "#help"}, {2, "#lobby"},
    };
    for (const auto& c : chans) openConversation(c.net, QString::fromLatin1(c.chan));
    arrangeWindows(/*tile=*/false);  // cascade — this is what used to shrink the dock
}

void MainWindow::noteMessage(Buffer* buffer, long long id) {
    if (!buffer) return;
    if (id > buffer->lastMessageId) buffer->lastMessageId = id;
    // Reading a buffer you're looking at keeps it read on every device.
    if (bufferIsActive(*buffer)) {
        if (id > 0 && m_backend) m_backend->markRead(buffer->networkId, buffer->display, id);
        if (buffer->unread != 0) {
            buffer->unread = 0;
            updateBufferLabel(buffer);
        }
    }
}

void MainWindow::arrangeWindows(bool tile) {
    // Tile/Cascade only the windows the user has actually opened (visible ones) —
    // never force-show all of the (possibly dozens of) channel buffers — and lay
    // them out uniformly ourselves so every window is the same size.
    m_windowsMaximized = false;
    m_tiled = tile;
    QList<QMdiSubWindow*> subs;
    for (QMdiSubWindow* s : m_mdi->subWindowList()) {
        if (!s->isVisible()) continue;
        if (s->isMinimized() || s->isMaximized()) s->showNormal();
        subs << s;
    }
    if (subs.isEmpty()) return;
    const QSize area = m_mdi->viewport()->size();

    if (tile) {
        const int n = subs.size();
        int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
        int rows = (n + cols - 1) / cols;
        const int w = area.width() / cols;
        const int h = area.height() / rows;
        for (int i = 0; i < n; ++i) {
            const int r = i / cols, c = i % cols;
            subs[i]->setGeometry(c * w, r * h, w, h);
        }
    } else {
        // Uniform cascade: same size, offset diagonally, wrapping at the edge.
        const QSize win(qMax(380, area.width() * 3 / 5), qMax(260, area.height() * 3 / 5));
        const int step = 26;
        int x = 0, y = 0;
        for (QMdiSubWindow* s : subs) {
            s->resize(win);
            s->move(x, y);
            x += step;
            y += step;
            if (x + win.width() > area.width() || y + win.height() > area.height()) {
                x = 0;
                y = 0;
            }
        }
        subs.last()->raise();
    }
}

void MainWindow::maximizeWindows() {
    m_windowsMaximized = true;
    m_tiled = false;
    if (QMdiSubWindow* active = m_mdi->activeSubWindow()) {
        active->showMaximized();
    } else if (!m_buffers.isEmpty()) {
        for (auto& b : m_buffers)
            if (b.sub && b.sub->isVisible()) {
                b.sub->showMaximized();
                break;
            }
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // Re-tile so the grid tracks the window size (maximized windows refit on their
    // own; cascaded/free ones keep their positions, like mIRC).
    if (m_tiled && !m_closing) arrangeWindows(true);
}

void MainWindow::activateBuffer(Buffer* buffer) {
    if (!buffer || !buffer->sub) return;
    if (!buffer->sub->isVisible()) buffer->sub->show();
    // Match the current workspace mode: maximized (mIRC default — one window fills
    // the area and switching swaps it) or free-floating after a Tile/Cascade.
    if (m_windowsMaximized) buffer->sub->showMaximized();
    else if (buffer->sub->isMinimized()) buffer->sub->showNormal();
    m_mdi->setActiveSubWindow(buffer->sub);
    buffer->view->focusInput();
    hydrateBuffer(buffer);
    if (buffer->lastMessageId > 0 && m_backend) m_backend->markRead(buffer->networkId, buffer->display, buffer->lastMessageId);
    if (buffer->unread != 0) {
        buffer->unread = 0;
        updateBufferLabel(buffer);
    }
}

void MainWindow::hydrateBuffer(Buffer* buffer) {
    // Fetch backlog the first time a conversation buffer is viewed: the snapshot
    // only ships shells, so content arrives on demand (CLIENT_PROTOCOL §4.3).
    if (!buffer || buffer->hydrated) return;
    if (buffer->kind == ChatView::Kind::Status) return;
    if (!m_backend || !m_backend->isReady()) return;
    buffer->hydrated = true;
    m_backend->requestHistory(buffer->networkId, buffer->display);
}

QString MainWindow::timestampFor(const std::string& isoTime) const {
    QSettings s;
    if (!s.value(QStringLiteral("display/timestamps"), true).toBool()) return {};
    QDateTime dt;
    if (!isoTime.empty()) {
        dt = QDateTime::fromString(QString::fromStdString(isoTime), Qt::ISODateWithMs);
        if (dt.isValid()) dt = dt.toLocalTime();
    }
    if (!dt.isValid()) dt = QDateTime::currentDateTime();
    return dt.toString(s.value(QStringLiteral("display/timeFormat"), QStringLiteral("HH:mm")).toString());
}

void MainWindow::onStatusText(const QString& text) {
    if (Buffer* s = findBuffer(statusKey())) s->view->addSystemLine(text.toUtf8());
    statusBar()->showMessage(text, 6000);
}

void MainWindow::onNetworkUpdated(const NetworkInfo& info) {
    m_networkNames.insert(info.id, info.name.isEmpty() ? tr("Network %1").arg(info.id) : info.name);
    if (!info.nick.isEmpty()) m_networkNicks.insert(info.id, info.nick);
    if (auto* node = m_networkNodes.value(info.id)) node->setText(0, m_networkNames.value(info.id));
    else networkNode(info.id)->setText(0, m_networkNames.value(info.id));
    // A per-network server buffer collects that network's server text.
    Buffer& server = ensureBuffer(info.id, ChatView::Kind::Status, m_networkNames.value(info.id), serverKeyFor(info.id));
    server.view->addSystemLine(tr("%1: %2 as %3").arg(m_networkNames.value(info.id), info.state, info.nick).toUtf8());
}

void MainWindow::onBufferOpened(int net, const QString& rawTarget, int kind) {
    const QString target = normTarget(rawTarget);
    // The per-network server buffer already exists as the network node.
    if (target == QStringLiteral(":server:")) return;
    ensureBuffer(net, kindFromInt(kind), target, keyFor(net, target));
}

MainWindow::Buffer* MainWindow::routeFor(const QString& category, const QString& def, int net) {
    const QString where = QSettings().value(QStringLiteral("route/") + category, def).toString();
    Buffer* status = findBuffer(serverKeyFor(net));
    if (!status) status = findBuffer(statusKey());
    if (where.contains(QStringLiteral("Status"), Qt::CaseInsensitive)) return status;
    // "Active window": the focused buffer, falling back to status.
    if (Buffer* a = activeBuffer()) return a;
    return status;
}

MainWindow::Buffer* MainWindow::routeTarget(int net, const QString& rawTarget, ChatView::Kind fallback) {
    const QString target = normTarget(rawTarget);
    if (target.isEmpty() || target == QStringLiteral(":server:")) {
        Buffer* s = findBuffer(serverKeyFor(net));
        return s ? s : findBuffer(statusKey());
    }
    const QString key = keyFor(net, target);
    if (Buffer* b = findBuffer(key)) return b;
    const ChatView::Kind kind = looksLikeChannel(target) ? ChatView::Kind::Channel : fallback;
    return &ensureBuffer(net, kind, target, key);
}

void MainWindow::onEvent(const IrcEvent& event) {
    const int net = event.networkId;
    const QString ts = timestampFor(event.time);
    const QString nick = QString::fromStdString(event.nick);
    const QByteArray text = QByteArray::fromStdString(event.text);
    const QString target = QString::fromStdString(event.target);
    const bool showJoins = QSettings().value(QStringLiteral("irc/showJoins"), true).toBool();

    // /list replies (RPL_LISTSTART 321 / RPL_LIST 322 / RPL_LISTEND 323) go to the
    // dedicated Channels List window instead of spamming the status window.
    if (event.type == IrcEventType::ServerText &&
        (event.numeric == 321 || event.numeric == 322 || event.numeric == 323)) {
        if (event.numeric == 321) {
            ensureChannelList(net)->listBegin();
        } else if (event.numeric == 323) {
            if (m_channelList) m_channelList->listEnd();
        } else {
            // 322: <me> <channel> <count> :<topic>
            QString chan = QString::fromStdString(event.raw.param(1));
            int users = QString::fromStdString(event.raw.param(2)).toInt();
            QString topic = QString::fromStdString(event.raw.param(3));
            if (chan.isEmpty()) {  // WS/other paths may only give rendered text
                const QStringList parts = QString::fromUtf8(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                if (!parts.isEmpty()) chan = parts[0];
                if (parts.size() > 1) users = parts[1].toInt();
                if (parts.size() > 2) topic = QStringList(parts.mid(2)).join(QLatin1Char(' '));
            }
            if (!chan.isEmpty()) ensureChannelList(net)->listAdd(chan, users, topic);
        }
        return;
    }

    // Notify list: RPL_ISON (303) lists which watched nicks are online. Diff
    // against what we held and announce the transitions.
    if (event.type == IrcEventType::ServerText && event.numeric == 303) {
        QString body = QString::fromStdString(event.raw.param(1));
        if (body.isEmpty()) body = QString::fromUtf8(text);
        const QStringList online = body.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QSet<QString> nowOnline;
        for (const QString& n : online) nowOnline.insert(n.toLower());
        for (const QString& n : m_notifyList) {
            const bool was = m_notifyOnline.contains(n.toLower());
            const bool is = nowOnline.contains(n.toLower());
            if (is && !was) onStatusText(tr("%1 is on IRC").arg(n));
            else if (!is && was) onStatusText(tr("%1 is no longer on IRC").arg(n));
        }
        m_notifyOnline = nowOnline;
        if (m_addressBook) m_addressBook->setNotifyOnline(online);
        return;
    }

    // Channel Central: current modes (RPL_CHANNELMODEIS 324) and the ban list
    // (RPL_BANLIST 367 / end 368) feed the open dialog instead of the status window.
    if (event.type == IrcEventType::ServerText &&
        (event.numeric == 324 || event.numeric == 367 || event.numeric == 368)) {
        // Parameters, from the raw line or (WS backend) the rendered text.
        QStringList parts;
        if (!event.raw.param(1).empty()) {
            for (int i = 1; i <= 24; ++i) {
                const std::string p = event.raw.param(i);
                if (p.empty() && i > 1) break;
                parts << QString::fromStdString(p);
            }
        } else {
            parts = QString::fromUtf8(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        }
        const QString chan = parts.isEmpty() ? QString() : parts[0];
        if (ChannelCentralDialog* d = m_channelCentrals.value(keyFor(net, chan))) {
            if (event.numeric == 324 && parts.size() >= 2) {
                const QString modes = parts[1];
                QString key;
                int limit = 0, pi = 2;
                for (const QChar& c : modes) {
                    if (c == QLatin1Char('k') && pi < parts.size()) key = parts[pi++];
                    else if (c == QLatin1Char('l') && pi < parts.size()) limit = parts[pi++].toInt();
                }
                d->setCurrentModes(modes, key, limit);
            } else if (event.numeric == 367 && parts.size() >= 2) {
                d->addBanMask(parts[1]);
            }
            return;  // consumed by the dialog; don't echo to status
        }
    }

    switch (event.type) {
        case IrcEventType::Message:
        case IrcEventType::Action: {
            if (isIgnored(event)) break;  // Address Book ignore list: drop silently
            Buffer* buf = routeTarget(net, target.isEmpty() ? nick : target, ChatView::Kind::Query);
            if (buf) {
                const bool act = event.type == IrcEventType::Action;
                const QString plain = stripFormattingQt(text);
                bool hl = event.highlight;
                if (!hl && !event.self) hl = highlightMatch(net, plain);
                // With an MTS theme active, render the line through its message
                // template (TEXT/ACTION × CHAN/QUERY × SELF). URL-catching,
                // highlight and logging below still run; reactions/reply quotes
                // are dropped under a theme (its line format owns the row).
                bool themed = false;
                if (lurker::gui::activeMtsTheme()) {
                    const bool query = buf->kind == ChatView::Kind::Query;
                    const QString key = QStringLiteral("%1%2%3")
                                            .arg(act ? QStringLiteral("ACTION") : QStringLiteral("TEXT"),
                                                 query ? QStringLiteral("QUERY") : QStringLiteral("CHAN"),
                                                 event.self ? QStringLiteral("SELF") : QString());
                    const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                    {QStringLiteral("me"), m_networkNicks.value(net)},
                                                    {QStringLiteral("text"), QString::fromUtf8(text)},
                                                    {QStringLiteral("chan"), target},
                                                    {QStringLiteral("cmode"), QString()}};
                    themed = renderThemedEvent(buf, key, v, ts);
                }
                if (!themed) {
                    // Reply context (a dim quote above the line) and any reactions
                    // already standing on it (IRCv3 replies/reactions, #5).
                    const QString replyNick =
                        event.replyParent.valid ? QString::fromStdString(event.replyParent.nick) : QString();
                    const QString replyText =
                        event.replyParent.valid
                            ? stripFormattingQt(QByteArray::fromStdString(event.replyParent.text))
                            : QString();
                    buf->view->addMessage(ts, nick, text, act, event.self, hl, event.id, replyNick, replyText);
                    for (const auto& r : event.reactions)
                        buf->view->applyReaction(event.id, QString::fromStdString(r.nick),
                                                 QString::fromStdString(r.value), r.self, false);
                }
                noteMessage(buf, event.id);
                catchUrls(nick, buf->display, plain);
                logLine(buf, act ? QStringLiteral("* %1 %2").arg(nick, plain)
                                 : QStringLiteral("<%1> %2").arg(nick, plain));
                if (hl && !event.self && !bufferIsActive(*buf)) {
                    if (QSettings().value(QStringLiteral("sound/beepHighlight"), true).toBool()) QApplication::beep();
                    if (QSettings().value(QStringLiteral("highlight/flash"), true).toBool()) QApplication::alert(this);
                }
            }
            break;
        }
        case IrcEventType::Notice: {
            if (isIgnored(event)) break;  // ignore list applies to notices too
            // A *server* notice (snote) comes from the server itself: no source
            // nick, or a server-name source (contains '.'). A service such as
            // OperServ/NickServ has a plain nick, so its notice is a USER notice
            // and must honour the route/notice preference — Lurker often omits a
            // service's user@host, so classifying by that sent service replies to
            // the server buffer where they were easy to miss (#24).
            const bool serverNotice = nick.isEmpty() || nick.contains(QLatin1Char('.'));
            Buffer* buf = nullptr;
            if (serverNotice) {
                buf = findBuffer(serverKeyFor(net));
            } else {
                const QString where = QSettings().value(QStringLiteral("route/notice"),
                                                        QStringLiteral("Active window")).toString();
                if (where.contains(QStringLiteral("query"), Qt::CaseInsensitive) ||
                    where.contains(QStringLiteral("Separate"), Qt::CaseInsensitive))
                    // Open the query with the other party: the sender for an
                    // incoming notice, the target for our own echoed notice.
                    buf = routeTarget(net, event.self ? target : QString::fromStdString(event.nick),
                                      ChatView::Kind::Query);
                else
                    buf = routeFor(QStringLiteral("notice"), QStringLiteral("Active window"), net);
            }
            if (!buf) buf = findBuffer(statusKey());
            if (buf) {
                buf->view->addNotice(ts, nick.isEmpty() ? tr("server") : nick, text, serverNotice);
                noteMessage(buf, event.id);
                catchUrls(nick.isEmpty() ? tr("server") : nick, buf->display, stripFormattingQt(text));
                logLine(buf, QStringLiteral("-%1- %2").arg(nick, stripFormattingQt(text)));
            }
            break;
        }
        case IrcEventType::Join: {
            Buffer* buf = routeTarget(net, target, ChatView::Kind::Channel);
            if (buf && (event.self || showJoins)) {
                const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                {QStringLiteral("address"), QString::fromStdString(event.user + "@" + event.host)},
                                                {QStringLiteral("chan"), target}};
                if (renderThemedEvent(buf, event.self ? QStringLiteral("JOINSELF") : QStringLiteral("JOIN"), v, ts))
                    break;
                buf->view->addSystemLine(event.self ? tr("Now talking in %1").arg(target).toUtf8()
                                                    : tr("%1 joined").arg(nick).toUtf8(),
                                         ChatView::SystemKind::Join);
            }
            break;
        }
        case IrcEventType::Part:
            if (!showJoins) break;
            if (Buffer* b = findBuffer(keyFor(net, target))) {
                const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                {QStringLiteral("address"), QString::fromStdString(event.user + "@" + event.host)},
                                                {QStringLiteral("chan"), target},
                                                {QStringLiteral("text"), QString::fromUtf8(text)}};
                if (renderThemedEvent(b, QStringLiteral("PART"), v, ts)) break;
                b->view->addSystemLine(tr("%1 left%2").arg(nick, text.isEmpty() ? QString() :
                                           QStringLiteral(" (%1)").arg(QString::fromUtf8(text))).toUtf8(),
                                       ChatView::SystemKind::Part);
            }
            break;
        case IrcEventType::Quit:
            if (!showJoins) break;
            if (Buffer* b = findBuffer(keyFor(net, target))) {
                const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                {QStringLiteral("address"), QString::fromStdString(event.user + "@" + event.host)},
                                                {QStringLiteral("text"), QString::fromUtf8(text)}};
                if (renderThemedEvent(b, QStringLiteral("QUIT"), v, ts)) break;
                b->view->addSystemLine(tr("%1 quit%2").arg(nick, text.isEmpty() ? QString() :
                                           QStringLiteral(" (%1)").arg(QString::fromUtf8(text))).toUtf8(),
                                       ChatView::SystemKind::Quit);
            }
            break;
        case IrcEventType::NickChange:
            if (!target.isEmpty())
                if (Buffer* b = findBuffer(keyFor(net, target))) {
                    const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                    {QStringLiteral("newnick"), QString::fromStdString(event.arg)}};
                    if (renderThemedEvent(b, event.self ? QStringLiteral("NICKSELF") : QStringLiteral("NICK"), v, ts))
                        break;
                    b->view->addSystemLine(tr("%1 is now known as %2").arg(nick, QString::fromStdString(event.arg)).toUtf8(),
                                           ChatView::SystemKind::Nick);
                }
            break;
        case IrcEventType::Kick:
            if (Buffer* b = findBuffer(keyFor(net, target))) {
                const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                {QStringLiteral("knick"), QString::fromStdString(event.arg)},
                                                {QStringLiteral("chan"), target},
                                                {QStringLiteral("text"), QString::fromUtf8(text)}};
                if (renderThemedEvent(b, event.self ? QStringLiteral("KICKSELF") : QStringLiteral("KICK"), v, ts))
                    break;
                b->view->addSystemLine(tr("%1 was kicked by %2").arg(QString::fromStdString(event.arg), nick).toUtf8(),
                                       ChatView::SystemKind::Kick);
            }
            break;
        case IrcEventType::Mode:
            if (Buffer* b = routeTarget(net, target, ChatView::Kind::Channel)) {
                const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                {QStringLiteral("modes"), QString::fromUtf8(text)},
                                                {QStringLiteral("chan"), target}};
                if (renderThemedEvent(b, QStringLiteral("MODE"), v, ts)) break;
                b->view->addSystemLine(tr("%1 set mode %2").arg(nick, QString::fromUtf8(text)).toUtf8(),
                                       ChatView::SystemKind::Mode);
            }
            break;
        case IrcEventType::Topic:
            if (Buffer* b = routeTarget(net, target, ChatView::Kind::Channel)) {
                const QHash<QString, QString> v{{QStringLiteral("nick"), nick},
                                                {QStringLiteral("text"), QString::fromUtf8(text)},
                                                {QStringLiteral("chan"), target}};
                if (renderThemedEvent(b, QStringLiteral("TOPIC"), v, ts)) break;
                b->view->addSystemLine(tr("%1 changed the topic").arg(nick).toUtf8(), ChatView::SystemKind::Topic);
            }
            break;
        case IrcEventType::Invite:
            if (Buffer* s = findBuffer(serverKeyFor(net)))
                s->view->addSystemLine(tr("%1 invites you to %2").arg(nick, QString::fromStdString(event.arg)).toUtf8());
            break;
        case IrcEventType::ServerText: {
            // Whois/who/info replies can follow the "whois" routing setting
            // (Active window or Status); other server text stays in status.
            static const QSet<int> whoisNums = {311, 312, 313, 317, 318, 319, 301, 330, 338,
                                                378, 379, 671, 276, 307, 310, 320, 314, 369};
            Buffer* buf = whoisNums.contains(event.numeric) ? routeFor(QStringLiteral("whois"),
                                                                       QStringLiteral("Active window"), net)
                                                            : findBuffer(serverKeyFor(net));
            if (buf) buf->view->addMessage(ts, QString(), text);
            else onStatusText(QString::fromUtf8(text));
            break;
        }
        case IrcEventType::MotdLine:
        case IrcEventType::AwaySelf:
            if (Buffer* s = findBuffer(serverKeyFor(net))) s->view->addMessage(ts, QString(), text);
            else onStatusText(QString::fromUtf8(text));
            break;
        case IrcEventType::CtcpReply:
            if (Buffer* b = routeFor(QStringLiteral("ctcp"), QStringLiteral("Active window"), net))
                b->view->addSystemLine(tr("[%1 %2 reply] %3")
                                           .arg(nick, QString::fromStdString(event.arg), QString::fromUtf8(text))
                                           .toUtf8());
            break;
        case IrcEventType::ErrorText:
            if (Buffer* s = findBuffer(serverKeyFor(net))) s->view->addSystemLine(QByteArray("⚠ ") + text);
            else onStatusText(QStringLiteral("⚠ ") + QString::fromUtf8(text));
            break;
        case IrcEventType::Registered:
            if (Buffer* s = findBuffer(serverKeyFor(net))) s->view->addSystemLine(tr("Registered as %1").arg(nick).toUtf8());
            break;
        default:
            break;
    }

    // Fire MSL remote events (on TEXT / ACTION / JOIN / ...) after rendering.
    dispatchScriptEvent(event);
}

void MainWindow::onMembersReplaced(int net, const QString& target, const QList<DisplayMember>& members) {
    Buffer* buf = findBuffer(keyFor(net, target));
    if (!buf || buf->kind != ChatView::Kind::Channel) return;
    QStringList sigils, nicks;
    for (const DisplayMember& m : members) {
        sigils << m.sigil;
        nicks << m.nick;
    }
    buf->memberNicks = nicks;
    buf->view->setMembers(sigils, nicks);
    buf->sub->setWindowTitle(QStringLiteral("%1 (%2)").arg(buf->display).arg(nicks.size()));
}

void MainWindow::onTopicChanged(int net, const QString& target, const QByteArray& topic) {
    if (Buffer* b = findBuffer(keyFor(net, target))) b->view->setTopic(topic);
}

bool MainWindow::bufferIsActive(const Buffer& buffer) const {
    return buffer.sub && m_mdi->activeSubWindow() == buffer.sub && !buffer.sub->isMinimized();
}

QString MainWindow::treeLabel(const Buffer& b) const {
    // Under the combined Favourites node, channels from different networks mix,
    // so prefix the network to keep same-named channels apart.
    QString base = b.display;
    if (m_favouritesNode && b.treeItem && b.treeItem->parent() == m_favouritesNode)
        base = m_networkNames.value(b.networkId, QString::number(b.networkId)) + QLatin1Char('/') + b.display;
    return b.unread > 0 ? QStringLiteral("%1 (%2)").arg(base).arg(b.unread) : base;
}

void MainWindow::updateBufferLabel(Buffer* buffer) {
    if (!buffer || !buffer->treeItem) return;
    buffer->treeItem->setText(0, treeLabel(*buffer));
    QFont f = buffer->treeItem->font(0);
    f.setBold(buffer->unread > 0);
    buffer->treeItem->setFont(0, f);
    if (buffer->switchAction) {  // the switchbar is a flat list → plain name, no prefix
        buffer->switchAction->setText(
            buffer->unread > 0 ? QStringLiteral("%1 (%2)").arg(buffer->display).arg(buffer->unread) : buffer->display);
    }
}

void MainWindow::reapplyPinPlacement() {
    // The combined/per-network mode changed: re-place every channel row.
    for (auto& b : m_buffers)
        if (b.kind == ChatView::Kind::Channel) repositionPinned(&b);
    pruneFavouritesNode();
}

void MainWindow::onReadStateChanged(int net, const QString& rawTarget, int unread, int /*highlights*/) {
    if (Buffer* b = findBuffer(keyFor(net, normTarget(rawTarget)))) {
        b->unread = bufferIsActive(*b) ? 0 : unread;
        updateBufferLabel(b);
    }
}

void MainWindow::onInput(Buffer* buffer, const QString& text) {
    if (!buffer) return;
    // /react <value> and /unreact <value>: (un)react to this buffer's most recent
    // message with an IRCv3 reaction (Lurker). Client-only, so handle it here
    // before the IRC/MSL dispatch. The server echoes a reaction frame that draws
    // the chip; nothing is rendered optimistically.
    if (text.startsWith(QStringLiteral("/react ")) || text.startsWith(QStringLiteral("/unreact "))) {
        const bool remove = text.startsWith(QStringLiteral("/unreact"));
        const QString value = text.section(QLatin1Char(' '), 1).trimmed();
        if (!m_backend || !m_backend->supportsReactions())
            buffer->view->addSystemLine(tr("Reactions need a Lurker connection.").toUtf8());
        else if (buffer->lastMessageId <= 0)
            buffer->view->addSystemLine(tr("No message here to react to yet.").toUtf8());
        else if (!value.isEmpty())
            m_backend->react(buffer->lastMessageId, value, remove);
        return;
    }
    // MSL entry points: "//" forces evaluation; a bare "/alias" invokes a
    // user-defined alias. Everything else goes through the IRC command parser.
    if (text.startsWith(QStringLiteral("//"))) {
        runScript(text);
        return;
    }
    if (text.startsWith(QLatin1Char('/')) && !text.startsWith(QStringLiteral("//")) && m_msl) {
        QString word = text.mid(1).section(QLatin1Char(' '), 0, 0).toLower();
        // A leading '.' is mIRC's silent prefix (/.msg); strip it for lookup and
        // always route dotted commands to the MSL engine (which handles the dot).
        const bool dotted = word.startsWith(QLatin1Char('.'));
        while (word.startsWith(QLatin1Char('.'))) word = word.mid(1);
        // User aliases always win. For built-in commands, let the integrated IRC
        // command parser keep the ones it handles best (buffer-aware /join, /msg,
        // /query, …) and route everything else the MSL engine knows to MSL — so
        // /switchbar, /toolbar, /amsg, /echo, /window, /timer, … work from the box.
        static const QSet<QString> keepParser = {
            QStringLiteral("join"),    QStringLiteral("j"),       QStringLiteral("part"),
            QStringLiteral("leave"),   QStringLiteral("msg"),     QStringLiteral("m"),
            QStringLiteral("me"),      QStringLiteral("query"),   QStringLiteral("q"),
            QStringLiteral("notice"),  QStringLiteral("topic"),   QStringLiteral("away"),
            QStringLiteral("back"),    QStringLiteral("mode"),    QStringLiteral("kick"),
            QStringLiteral("invite"),  QStringLiteral("op"),      QStringLiteral("deop"),
            QStringLiteral("voice"),   QStringLiteral("devoice"), QStringLiteral("whois"),
            QStringLiteral("who"),     QStringLiteral("w"),       QStringLiteral("quit"),
            QStringLiteral("quote"),   QStringLiteral("raw"),     QStringLiteral("ctcp"),
            QStringLiteral("describe"),QStringLiteral("list"),    QStringLiteral("clear"),
            QStringLiteral("close"),   QStringLiteral("server"),  QStringLiteral("disconnect"),
            QStringLiteral("nick"),    QStringLiteral("version"), QStringLiteral("time"),
            QStringLiteral("exit"),    QStringLiteral("help")};
        if (m_msl->hasAlias(word.toStdString()) ||
            (m_msl->hasCommand(word.toStdString()) && (dotted || !keepParser.contains(word)))) {
            runScript(text.mid(1));
            return;
        }
    }
    lurker::ParseContext ctx;
    ctx.kind = buffer->kind == ChatView::Kind::Channel  ? lurker::BufferKind::Channel
               : buffer->kind == ChatView::Kind::Query  ? lurker::BufferKind::Dm
                                                        : lurker::BufferKind::Server;
    ctx.target = buffer->display.toStdString();

    const lurker::ParseResult result = lurker::parseInput(text.toStdString(), ctx);
    if (!result.ok()) {
        buffer->view->addSystemLine(QByteArray("⚠ ") + QByteArray::fromStdString(result.error));
        return;
    }
    const int net = buffer->networkId;
    for (const lurker::Action& a : result.actions) {
        using lurker::ActionKind;
        const QString target = QString::fromStdString(a.target);
        // Local actions work whether or not a network is connected.
        switch (a.kind) {
            case ActionKind::CloseBuffer: closeBuffer(buffer); continue;
            case ActionKind::LocalClear: buffer->view->clearMessages(); continue;
            case ActionKind::LocalHelp:
                buffer->view->addSystemLine(QByteArray::fromStdString(lurker::helpText()));
                continue;
            case ActionKind::OpenBuffer:
                activateBuffer(&ensureBuffer(net, ChatView::Kind::Query, target, keyFor(net, target)));
                continue;
            default: break;  // the rest need the network
        }
        if (!m_backend || !m_backend->isReady()) {
            buffer->view->addSystemLine(tr("Not connected.").toUtf8());
            continue;
        }
        switch (a.kind) {
            case ActionKind::Send: m_backend->sendMessage(net, target, QString::fromStdString(a.text)); break;
            case ActionKind::Action: m_backend->sendAction(net, target, QString::fromStdString(a.text)); break;
            case ActionKind::Notice: m_backend->sendNotice(net, target, QString::fromStdString(a.text)); break;
            case ActionKind::Raw: m_backend->sendRaw(net, QString::fromStdString(a.line)); break;
            case ActionKind::Join: m_backend->joinChannel(net, QString::fromStdString(a.channel), QString::fromStdString(a.key)); break;
            case ActionKind::Part: m_backend->partChannel(net, QString::fromStdString(a.channel), QString::fromStdString(a.reason)); break;
            case ActionKind::SetTopic: m_backend->setTopic(net, QString::fromStdString(a.channel), QString::fromStdString(a.text)); break;
            case ActionKind::Away: m_backend->setAway(net, QString::fromStdString(a.text)); break;
            case ActionKind::Back: m_backend->clearAway(net); break;
            case ActionKind::Ctcp: m_backend->sendCtcp(net, target, QString::fromStdString(a.ctcpType), QString::fromStdString(a.text)); break;
            case ActionKind::ListChannels: m_backend->requestChannelList(net); break;
            default: break;  // local actions handled above
        }
    }
}

void MainWindow::showServerList() {
    auto* d = new ServerListDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &ServerListDialog::connectRequested, this, [this](const QString& host, int port, bool tls) {
        QSettings s;  // prefill the connection, then open Connect for credentials
        s.setValue(QStringLiteral("connect/host"), host);
        s.setValue(QStringLiteral("connect/port"), port);
        s.setValue(QStringLiteral("connect/ssl"), tls);
        showConnectDialog();
    });
    d->show();
}

void MainWindow::showConnectDialog() {
    ConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    useBackend(dialog.createBackend(this));
    if (Buffer* s = findBuffer(statusKey())) {
        m_mdi->setActiveSubWindow(s->sub);
        s->view->focusInput();
    }
}

}  // namespace lurker::gui
