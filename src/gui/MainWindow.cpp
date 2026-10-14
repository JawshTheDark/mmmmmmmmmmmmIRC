// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
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

#include "core/CommandParser.h"
#include "gui/ConnectDialog.h"
#include "gui/PictureWindow.h"
#include "gui/ScriptDialog.h"
#include "gui/OptionsDialog.h"
#include "gui/ScriptEditor.h"
#include "gui/ScriptSockets.h"
#include "gui/Theme.h"
#include "gui/ToolbarIcons.h"

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
    setWindowTitle(QStringLiteral("Lurker Desktop"));
    resize(1180, 760);

    m_mdi = new QMdiArea(this);
    setCentralWidget(m_mdi);

    auto* treeDock = new QDockWidget(tr("Networks"), this);
    treeDock->setObjectName(QStringLiteral("treebar"));
    m_treebar = new QTreeWidget(treeDock);
    m_treebar->setHeaderHidden(true);
    treeDock->setWidget(m_treebar);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);
    connect(m_treebar, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        for (auto& b : m_buffers) {
            if (b.treeItem == item) {
                activateBuffer(&b);
                break;
            }
        }
    });

    m_switchbar = new QToolBar(tr("Windows"), this);
    m_switchbar->setObjectName(QStringLiteral("switchbar"));
    addToolBar(Qt::BottomToolBarArea, m_switchbar);
    m_switchGroup = new QActionGroup(this);

    auto* file = menuBar()->addMenu(tr("&File"));
    m_connectAction = file->addAction(tr("&Connect..."), QKeySequence(QStringLiteral("Ctrl+N")), this,
                                      &MainWindow::showConnectDialog);
    m_disconnectAction = file->addAction(tr("&Disconnect"), this, [this] {
        if (m_backend) m_backend->disconnectAll();
    });
    m_disconnectAction->setEnabled(false);
    file->addSeparator();
    file->addAction(tr("&Load Script..."), this, &MainWindow::promptLoadScript);
    file->addAction(tr("&Scripts Editor..."), this, &MainWindow::openScriptEditor);
    file->addAction(tr("&Options..."), QKeySequence(QStringLiteral("Alt+O")), this,
                    [this] { showOptions(); });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(tr("&Join Channel..."), QKeySequence(QStringLiteral("Ctrl+J")), this,
                         &MainWindow::showJoinDialog);
    toolsMenu->addAction(tr("Channels &List"), this, &MainWindow::requestChannelList);
    toolsMenu->addAction(tr("&Address Book..."), this, &MainWindow::showAddressBook);
    toolsMenu->addSeparator();
    toolsMenu->addAction(tr("&DCC..."), this, &MainWindow::showDccInfo);
    toolsMenu->addAction(tr("Open Logs &Folder"), this, &MainWindow::openLogsFolder);

    auto* windowMenu = menuBar()->addMenu(tr("&Window"));
    windowMenu->addAction(tr("&Cascade"), m_mdi, &QMdiArea::cascadeSubWindows);
    windowMenu->addAction(tr("&Tile"), m_mdi, &QMdiArea::tileSubWindows);
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
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Lurker Desktop..."), this, &MainWindow::showAbout);

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
    statusBar()->showMessage(tr("Not connected — File ▸ Connect"));
}

void MainWindow::setupToolbar() {
    m_toolbar = new QToolBar(tr("Toolbar"), this);
    m_toolbar->setObjectName(QStringLiteral("maintoolbar"));
    m_toolbar->setMovable(true);
    m_toolbar->setIconSize(QSize(24, 24));
    m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::TopToolBarArea, m_toolbar);

    const QColor fg = palette().color(QPalette::ButtonText);
    QColor accent = palette().color(QPalette::Highlight);
    if (!accent.isValid() || accent.lightness() > 220) accent = QColor(60, 120, 210);

    auto add = [&](Glyph g, const QString& text, auto slot) {
        QAction* a = m_toolbar->addAction(makeGlyph(g, fg, accent), text);
        a->setToolTip(text);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    add(Glyph::Connect, tr("Connect to a server"), &MainWindow::showConnectDialog);
    m_tbDisconnect = add(Glyph::Disconnect, tr("Disconnect"), [this] {
        if (m_backend) m_backend->disconnectAll();
    });
    m_tbDisconnect->setEnabled(false);
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
    add(Glyph::DccChat, tr("DCC Chat"), &MainWindow::showDccInfo);
    add(Glyph::Logs, tr("Open logs folder"), &MainWindow::openLogsFolder);
    m_toolbar->addSeparator();
    add(Glyph::About, tr("About Lurker Desktop"), &MainWindow::showAbout);
}

void MainWindow::showOptions(const QString& page) {
    OptionsDialog dlg(this);
    if (!page.isEmpty()) dlg.selectPage(page);
    if (dlg.exec() == QDialog::Accepted) {
        // Push display changes (font, scrollback) to every open conversation.
        for (auto& b : m_buffers) {
            if (b.view) b.view->applyDisplaySettings();
        }
    }
}

void MainWindow::promptLoadScript() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load MSL script"), m_scriptHost.baseDir,
                                                      tr("mIRC scripts (*.mrc);;All files (*)"));
    if (!path.isEmpty()) loadScriptFile(path);
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

void MainWindow::requestChannelList() {
    int net = -1;
    if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
    if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
    if (net < 0 || !m_backend || !m_backend->isReady()) {
        statusBar()->showMessage(tr("Connect to a server to list channels"), 6000);
        return;
    }
    m_backend->sendRaw(net, QStringLiteral("LIST"));
    statusBar()->showMessage(tr("Requested channel list — results appear in the Status window"), 6000);
}

void MainWindow::showAddressBook() {
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Favourites"));
    dlg.resize(360, 320);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(tr("Favourite channels — double-click to join:"), &dlg));
    auto* list = new QListWidget(&dlg);
    QSettings s;
    list->addItems(s.value(QStringLiteral("favourites/channels")).toStringList());
    v->addWidget(list, 1);
    auto* row = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add..."), &dlg);
    auto* delBtn = new QPushButton(tr("Remove"), &dlg);
    auto* joinBtn = new QPushButton(tr("Join"), &dlg);
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch(1);
    row->addWidget(joinBtn);
    v->addLayout(row);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);

    auto persist = [list] {
        QStringList items;
        for (int i = 0; i < list->count(); ++i) items << list->item(i)->text();
        QSettings().setValue(QStringLiteral("favourites/channels"), items);
    };
    connect(addBtn, &QPushButton::clicked, &dlg, [&] {
        bool ok = false;
        QString c = QInputDialog::getText(&dlg, tr("Add Favourite"), tr("Channel:"), QLineEdit::Normal,
                                          QStringLiteral("#"), &ok);
        c = c.trimmed();
        if (ok && !c.isEmpty() && c != QLatin1String("#")) {
            list->addItem(c);
            persist();
        }
    });
    connect(delBtn, &QPushButton::clicked, &dlg, [&] {
        qDeleteAll(list->selectedItems());
        persist();
    });
    auto joinSel = [&] {
        if (auto* it = list->currentItem()) {
            int net = -1;
            if (Buffer* b = activeBuffer(); b && b->networkId >= 0) net = b->networkId;
            if (net < 0 && !m_networkNames.isEmpty()) net = m_networkNames.keys().first();
            if (net >= 0 && m_backend && m_backend->isReady()) {
                m_backend->joinChannel(net, it->text(), QString());
                openConversation(net, it->text());
                dlg.accept();
            } else {
                statusBar()->showMessage(tr("Connect to a server first"), 6000);
            }
        }
    };
    connect(joinBtn, &QPushButton::clicked, &dlg, joinSel);
    connect(list, &QListWidget::itemDoubleClicked, &dlg, joinSel);
    dlg.exec();
}

void MainWindow::showDccInfo() {
    QSettings s;
    const QString dir =
        s.value(QStringLiteral("dcc/downloadDir"),
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
            .toString();
    QMessageBox::information(
        this, tr("DCC"),
        tr("<b>DCC transfers are proxied through Lurker.</b><br><br>"
           "Rather than opening direct peer-to-peer connections, sends and chats are relayed "
           "through your Lurker bouncer so they work behind NAT and from any device.<br><br>"
           "Download folder: <code>%1</code><br>"
           "Configure this under <b>Options ▸ DCC</b>. The server-side bridge is wired to "
           "Lurker's file host and <code>/api/dcc</code>.")
            .arg(dir.toHtmlEscaped()));
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
    const QString version = QStringLiteral(LURKER_DESKTOP_VERSION);
    QMessageBox::about(
        this, tr("About Lurker Desktop"),
        tr("<h3>Lurker Desktop %1</h3>"
           "<p>A clean-room, cross-platform IRC client for Lurker, built for full mIRC "
           "feature and scripting (MSL) parity.</p>"
           "<p>Qt %2 · built %3</p>"
           "<p>Licensed under the Mozilla Public License 2.0. Not affiliated with or derived "
           "from mIRC; no mIRC code or artwork is used.</p>")
            .arg(version, QStringLiteral(QT_VERSION_STR), QStringLiteral(__DATE__)));
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
        if (m_inEvent) return m_eventContext;  // during an on-event, "active" is the event's target
        Buffer* b = activeBuffer();
        if (!b) return {};
        return {b->networkId, b->display.toStdString()};
    };
    m_scriptHost.meFn = [this](int net) { return m_networkNicks.value(net); };
    m_scriptHost.errorFn = [this](const QString& text) {
        if (Buffer* b = activeBuffer()) b->view->addSystemLine(QByteArray("* ") + text.toUtf8());
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
        if (Buffer* b = findBuffer(keyFor(-1, name))) b->view->clearMessages();
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

void MainWindow::loadScriptFile(const QString& path) {
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
    onStatusText(tr("Loaded %1 file(s) — %2").arg(toLoad.size()).arg(scriptSummary()));
}

QString MainWindow::scriptSummary() const {
    return m_msl ? tr("%1 aliases, %2 event handlers").arg(m_msl->aliasCount()).arg(m_msl->eventHandlerCount())
                 : QString();
}

void MainWindow::reloadScripts() {
    if (!m_msl) return;
    m_msl->clearScripts();
    const QStringList files = m_scriptFiles;
    for (const QString& file : files) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) m_msl->loadScript(QString::fromUtf8(f.readAll()).toStdString());
    }
    onStatusText(tr("Reloaded scripts — %1").arg(scriptSummary()));
}

void MainWindow::openScriptEditor() {
    auto* ed = new ScriptEditor(this, [this] { return m_scriptFiles; }, [this] { reloadScripts(); },
                                [this] { return scriptSummary(); });
    ed->setAttribute(Qt::WA_DeleteOnClose);
    ed->show();
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
        default: return;
    }
    if (event.self || !m_msl->hasEvent(name.toStdString())) return;

    lurker::msl::Scope scope;
    scope.nick = event.nick;
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

MainWindow::Buffer& MainWindow::ensureBuffer(int net, ChatView::Kind kind, const QString& display, const QString& key) {
    if (Buffer* existing = findBuffer(key)) return *existing;

    Buffer b;
    b.kind = kind;
    b.networkId = net;
    b.display = display;
    b.key = key;
    b.view = new ChatView(kind);

    b.sub = m_mdi->addSubWindow(b.view);
    b.sub->setWindowTitle(display);
    b.sub->setAttribute(Qt::WA_DeleteOnClose);
    b.sub->resize(760, 480);
    // The first window (status) opens normally; later buffers open minimized so
    // dozens of channels don't bury the workspace — the treebar/switchbar drive
    // navigation and restore a window when its buffer is selected.
    if (m_buffers.isEmpty()) b.sub->show();
    else b.sub->showMinimized();

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

    b.switchAction = m_switchbar->addAction(display);
    b.switchAction->setCheckable(true);
    m_switchGroup->addAction(b.switchAction);

    if (kind == ChatView::Kind::Channel) {
        const QString key_ = key;
        b.view->setNickProvider([this, key_]() -> QStringList {
            Buffer* buf = findBuffer(key_);
            return buf ? buf->memberNicks : QStringList();
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
        }
    });

    return m_buffers.insert(key, b).value();
}

void MainWindow::useBackend(ChatBackend* backend) {
    if (m_backend) {
        m_backend->disconnect(this);
        m_backend->deleteLater();
    }
    m_backend = backend;
    backend->setParent(this);
    connect(backend, &ChatBackend::statusText, this, &MainWindow::onStatusText);
    connect(backend, &ChatBackend::networkUpdated, this, &MainWindow::onNetworkUpdated);
    connect(backend, &ChatBackend::bufferOpened, this, &MainWindow::onBufferOpened);
    connect(backend, &ChatBackend::event, this, &MainWindow::onEvent);
    connect(backend, &ChatBackend::membersReplaced, this, &MainWindow::onMembersReplaced);
    connect(backend, &ChatBackend::topicChanged, this, &MainWindow::onTopicChanged);
    connect(backend, &ChatBackend::readStateChanged, this, &MainWindow::onReadStateChanged);
    connect(backend, &ChatBackend::readyChanged, this, [this] {
        const bool ready = m_backend && m_backend->isReady();
        m_disconnectAction->setEnabled(ready);
        if (m_tbDisconnect) m_tbDisconnect->setEnabled(ready);
        statusBar()->showMessage(ready ? tr("Connected") : tr("Disconnected"));
    });
    connect(backend, &ChatBackend::closed, this, [this] { statusBar()->showMessage(tr("Disconnected")); });
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

void MainWindow::openConversation(int net, const QString& target) {
    const ChatView::Kind kind = looksLikeChannel(target) ? ChatView::Kind::Channel : ChatView::Kind::Query;
    activateBuffer(&ensureBuffer(net, kind, target, keyFor(net, target)));
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

void MainWindow::activateBuffer(Buffer* buffer) {
    if (!buffer || !buffer->sub) return;
    if (buffer->sub->isMinimized()) buffer->sub->showNormal();
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

    switch (event.type) {
        case IrcEventType::Message:
        case IrcEventType::Action: {
            Buffer* buf = routeTarget(net, target.isEmpty() ? nick : target, ChatView::Kind::Query);
            if (buf) {
                buf->view->addMessage(ts, nick, text, event.type == IrcEventType::Action, event.self, event.highlight);
                noteMessage(buf, event.id);
            }
            break;
        }
        case IrcEventType::Notice: {
            Buffer* buf = routeTarget(net, target, ChatView::Kind::Query);
            if (buf) {
                buf->view->addMessage(ts, nick + QStringLiteral(" (notice)"), text, false, event.self);
                noteMessage(buf, event.id);
            }
            break;
        }
        case IrcEventType::Join: {
            Buffer* buf = routeTarget(net, target, ChatView::Kind::Channel);
            if (buf) {
                if (event.self) buf->view->addSystemLine(tr("Now talking in %1").arg(target).toUtf8());
                else buf->view->addSystemLine(tr("%1 joined").arg(nick).toUtf8());
            }
            break;
        }
        case IrcEventType::Part:
            if (Buffer* b = findBuffer(keyFor(net, target)))
                b->view->addSystemLine(tr("%1 left%2").arg(nick, text.isEmpty() ? QString() :
                                           QStringLiteral(" (%1)").arg(QString::fromUtf8(text))).toUtf8());
            break;
        case IrcEventType::Quit:
            if (Buffer* b = findBuffer(keyFor(net, target)))
                b->view->addSystemLine(tr("%1 quit%2").arg(nick, text.isEmpty() ? QString() :
                                           QStringLiteral(" (%1)").arg(QString::fromUtf8(text))).toUtf8());
            break;
        case IrcEventType::NickChange:
            if (!target.isEmpty())
                if (Buffer* b = findBuffer(keyFor(net, target)))
                    b->view->addSystemLine(tr("%1 is now known as %2").arg(nick, QString::fromStdString(event.arg)).toUtf8());
            break;
        case IrcEventType::Kick:
            if (Buffer* b = findBuffer(keyFor(net, target)))
                b->view->addSystemLine(tr("%1 was kicked by %2").arg(QString::fromStdString(event.arg), nick).toUtf8());
            break;
        case IrcEventType::Mode:
            if (Buffer* b = routeTarget(net, target, ChatView::Kind::Channel))
                b->view->addSystemLine(tr("%1 set mode %2").arg(nick, QString::fromUtf8(text)).toUtf8());
            break;
        case IrcEventType::Topic:
            if (Buffer* b = routeTarget(net, target, ChatView::Kind::Channel))
                b->view->addSystemLine(tr("%1 changed the topic").arg(nick).toUtf8());
            break;
        case IrcEventType::Invite:
            if (Buffer* s = findBuffer(serverKeyFor(net)))
                s->view->addSystemLine(tr("%1 invites you to %2").arg(nick, QString::fromStdString(event.arg)).toUtf8());
            break;
        case IrcEventType::MotdLine:
        case IrcEventType::ServerText:
        case IrcEventType::AwaySelf:
            if (Buffer* s = findBuffer(serverKeyFor(net))) s->view->addMessage(ts, QString(), text);
            else onStatusText(QString::fromUtf8(text));
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

void MainWindow::updateBufferLabel(Buffer* buffer) {
    if (!buffer || !buffer->treeItem) return;
    const QString label =
        buffer->unread > 0 ? QStringLiteral("%1 (%2)").arg(buffer->display).arg(buffer->unread) : buffer->display;
    buffer->treeItem->setText(0, label);
    QFont f = buffer->treeItem->font(0);
    f.setBold(buffer->unread > 0);
    buffer->treeItem->setFont(0, f);
    if (buffer->switchAction) buffer->switchAction->setText(label);
}

void MainWindow::onReadStateChanged(int net, const QString& rawTarget, int unread, int /*highlights*/) {
    if (Buffer* b = findBuffer(keyFor(net, normTarget(rawTarget)))) {
        b->unread = bufferIsActive(*b) ? 0 : unread;
        updateBufferLabel(b);
    }
}

void MainWindow::onInput(Buffer* buffer, const QString& text) {
    if (!buffer) return;
    // MSL entry points: "//" forces evaluation; a bare "/alias" invokes a
    // user-defined alias. Everything else goes through the IRC command parser.
    if (text.startsWith(QStringLiteral("//"))) {
        runScript(text);
        return;
    }
    if (text.startsWith(QLatin1Char('/')) && m_msl) {
        const QString word = text.mid(1).section(QLatin1Char(' '), 0, 0).toLower();
        if (m_msl->hasAlias(word.toStdString())) {
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
    if (!m_backend || !m_backend->isReady()) {
        buffer->view->addSystemLine(tr("Not connected.").toUtf8());
        return;
    }
    const int net = buffer->networkId;
    for (const lurker::Action& a : result.actions) {
        using lurker::ActionKind;
        const QString target = QString::fromStdString(a.target);
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
            case ActionKind::ListChannels: m_backend->sendRaw(net, QStringLiteral("LIST")); break;
            case ActionKind::OpenBuffer:
                activateBuffer(&ensureBuffer(net, ChatView::Kind::Query, target, keyFor(net, target)));
                break;
            case ActionKind::CloseBuffer: closeBuffer(buffer); break;
            case ActionKind::LocalClear: buffer->view->addSystemLine(tr("(cleared)").toUtf8()); break;
            case ActionKind::LocalHelp: buffer->view->addSystemLine(QByteArray::fromStdString(lurker::helpText())); break;
        }
    }
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
