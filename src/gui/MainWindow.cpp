// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDockWidget>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "core/CommandParser.h"
#include "gui/ConnectDialog.h"

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
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto* windowMenu = menuBar()->addMenu(tr("&Window"));
    windowMenu->addAction(tr("&Cascade"), m_mdi, &QMdiArea::cascadeSubWindows);
    windowMenu->addAction(tr("&Tile"), m_mdi, &QMdiArea::tileSubWindows);
    windowMenu->addAction(treeDock->toggleViewAction());

    connect(m_mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow* w) {
        if (m_closing) return;
        for (auto& b : m_buffers) {
            if (b.sub == w && b.switchAction) b.switchAction->setChecked(true);
        }
    });

    ensureBuffer(-1, ChatView::Kind::Status, tr("Status"), statusKey());
    statusBar()->showMessage(tr("Not connected — File ▸ Connect"));
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
        statusBar()->showMessage(ready ? tr("Connected") : tr("Disconnected"));
    });
    connect(backend, &ChatBackend::closed, this, [this] { statusBar()->showMessage(tr("Disconnected")); });
}

void MainWindow::closeBuffer(Buffer* buffer) {
    if (buffer && buffer->key != statusKey() && buffer->sub) buffer->sub->close();
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
    QDateTime dt;
    if (!isoTime.empty()) {
        dt = QDateTime::fromString(QString::fromStdString(isoTime), Qt::ISODateWithMs);
        if (dt.isValid()) dt = dt.toLocalTime();
    }
    if (!dt.isValid()) dt = QDateTime::currentDateTime();
    return dt.toString(QStringLiteral("HH:mm"));
}

void MainWindow::onStatusText(const QString& text) {
    if (Buffer* s = findBuffer(statusKey())) s->view->addSystemLine(text.toUtf8());
    statusBar()->showMessage(text, 6000);
}

void MainWindow::onNetworkUpdated(const NetworkInfo& info) {
    m_networkNames.insert(info.id, info.name.isEmpty() ? tr("Network %1").arg(info.id) : info.name);
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
                buf->view->addMessage(ts, nick, text, event.type == IrcEventType::Action, event.self);
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
