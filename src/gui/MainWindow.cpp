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

#include "backend/BouncerBackend.h"
#include "core/CommandParser.h"
#include "gui/ConnectDialog.h"

namespace lurker::gui {

using lurker::IrcEvent;
using lurker::IrcEventType;
using lurker::backend::BouncerBackend;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Lurker Desktop"));
    resize(1120, 740);

    m_mdi = new QMdiArea(this);
    m_mdi->setViewMode(QMdiArea::SubWindowView);
    setCentralWidget(m_mdi);

    auto* treeDock = new QDockWidget(tr("Networks"), this);
    treeDock->setObjectName(QStringLiteral("treebar"));
    m_treebar = new QTreeWidget(treeDock);
    m_treebar->setHeaderHidden(true);
    treeDock->setWidget(m_treebar);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);
    connect(m_treebar, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        for (auto& b : m_buffers) {
            if (b.treeItem == item && b.sub) {
                m_mdi->setActiveSubWindow(b.sub);
                b.view->focusInput();
            }
        }
    });

    m_switchbar = new QToolBar(tr("Windows"), this);
    m_switchbar->setObjectName(QStringLiteral("switchbar"));
    addToolBar(Qt::BottomToolBarArea, m_switchbar);
    m_switchGroup = new QActionGroup(this);

    m_backend = new BouncerBackend(this);
    connect(m_backend, &BouncerBackend::eventReceived, this, &MainWindow::onEvent);
    connect(m_backend, &BouncerBackend::statusText, this, &MainWindow::onStatusText);
    connect(m_backend, &BouncerBackend::connectedAndRegistered, this, [this] {
        statusBar()->showMessage(tr("Connected"));
        m_disconnectAction->setEnabled(true);
    });
    connect(m_backend, &BouncerBackend::disconnected, this, [this] {
        statusBar()->showMessage(tr("Disconnected"));
        m_disconnectAction->setEnabled(false);
    });

    auto* file = menuBar()->addMenu(tr("&File"));
    m_connectAction = file->addAction(tr("&Connect..."), QKeySequence(QStringLiteral("Ctrl+N")), this,
                                      &MainWindow::showConnectDialog);
    m_disconnectAction = file->addAction(tr("&Disconnect"), this, [this] { m_backend->disconnectFromHost(); });
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

    // The status buffer always exists.
    ensureBuffer(ChatView::Kind::Status, tr("Status"), statusKey());
    statusBar()->showMessage(tr("Not connected — File ▸ Connect"));
}

MainWindow::~MainWindow() {
    // Stop reacting to child teardown: Qt will delete the MDI subwindows (and the
    // backend) during base destruction, and their destroyed/disconnected signals
    // would otherwise re-enter the buffer map and the status bar after this point.
    m_closing = true;
    if (m_backend) m_backend->disconnect(this);
}

QString MainWindow::bufferKeyFor(const QString& target) const {
    if (m_backend) return m_backend->casefold(target);
    return target.toLower();
}

MainWindow::Buffer* MainWindow::findBuffer(const QString& key) {
    const auto it = m_buffers.find(key);
    return it == m_buffers.end() ? nullptr : &it.value();
}

MainWindow::Buffer& MainWindow::ensureBuffer(ChatView::Kind kind, const QString& display, const QString& key) {
    if (Buffer* existing = findBuffer(key)) return *existing;

    Buffer b;
    b.kind = kind;
    b.display = display;
    b.key = key;
    b.view = new ChatView(kind);

    b.sub = m_mdi->addSubWindow(b.view);
    b.sub->setWindowTitle(display);
    b.sub->setAttribute(Qt::WA_DeleteOnClose);
    b.sub->resize(720, 460);
    b.sub->show();

    b.treeItem = new QTreeWidgetItem(m_treebar, {display});
    b.switchAction = m_switchbar->addAction(display);
    b.switchAction->setCheckable(true);
    m_switchGroup->addAction(b.switchAction);

    if (kind == ChatView::Kind::Channel) {
        const QString disp = display;
        b.view->setNickProvider([this, disp]() -> QStringList {
            QStringList out;
            if (m_backend && m_backend->session()) {
                if (const lurker::ChannelState* chan = m_backend->session()->channel(disp.toStdString())) {
                    for (const auto& [_, member] : chan->members) out << QString::fromStdString(member.nick);
                    out.sort(Qt::CaseInsensitive);
                }
            }
            return out;
        });
    }

    const QString key_ = key;
    connect(b.switchAction, &QAction::triggered, this, [this, key_] {
        if (Buffer* buf = findBuffer(key_)) {
            m_mdi->setActiveSubWindow(buf->sub);
            buf->view->focusInput();
        }
    });
    connect(b.view, &ChatView::lineEntered, this, [this, key_](const QString& text) {
        if (Buffer* buf = findBuffer(key_)) onInput(buf, text);
    });
    connect(b.view, &ChatView::nickActivated, this, [this](const QString& nick) {
        if (m_backend && m_backend->isRegistered()) {
            Buffer& q = ensureBuffer(ChatView::Kind::Query, nick, bufferKeyFor(nick));
            m_mdi->setActiveSubWindow(q.sub);
            q.view->focusInput();
        }
    });
    connect(b.sub, &QObject::destroyed, this, [this, key_](QObject*) {
        if (m_closing) return;
        if (Buffer* buf = findBuffer(key_)) {
            if (buf->switchAction) buf->switchAction->deleteLater();
            delete buf->treeItem;
            m_buffers.remove(key_);
        }
    });

    return m_buffers.insert(key, b).value();
}

void MainWindow::closeBuffer(Buffer* buffer) {
    if (buffer && buffer->key != statusKey() && buffer->sub) buffer->sub->close();
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

void MainWindow::refreshNicklist(const QString& channelDisplay) {
    Buffer* buf = findBuffer(bufferKeyFor(channelDisplay));
    if (!buf || buf->kind != ChatView::Kind::Channel || !m_backend || !m_backend->session()) return;
    const lurker::ChannelState* chan = m_backend->session()->channel(channelDisplay.toStdString());
    if (!chan) return;
    const lurker::Isupport& isupport = m_backend->session()->isupport();

    std::vector<const lurker::ChannelMember*> members;
    members.reserve(chan->members.size());
    for (const auto& [_, m] : chan->members) members.push_back(&m);
    std::sort(members.begin(), members.end(), [&](const lurker::ChannelMember* a, const lurker::ChannelMember* b) {
        const int ra = a->prefixModes.empty() ? INT_MAX : isupport.rankOfMode(a->prefixModes.front());
        const int rb = b->prefixModes.empty() ? INT_MAX : isupport.rankOfMode(b->prefixModes.front());
        if (ra != rb) return ra < rb;
        return m_backend->casefold(QString::fromStdString(a->nick)) <
               m_backend->casefold(QString::fromStdString(b->nick));
    });

    QStringList sigils, nicks;
    for (const lurker::ChannelMember* m : members) {
        const char sigil = m->prefixModes.empty() ? 0 : isupport.sigilForMode(m->prefixModes.front());
        sigils << (sigil ? QString(QChar(sigil)) : QString());
        nicks << QString::fromStdString(m->nick);
    }
    buf->view->setMembers(sigils, nicks);
    buf->sub->setWindowTitle(QStringLiteral("%1 (%2)").arg(buf->display).arg(nicks.size()));
}

void MainWindow::onEvent(const IrcEvent& event) {
    const QString ts = timestampFor(event.time);
    const QString nick = QString::fromStdString(event.nick);
    const QByteArray text = QByteArray::fromStdString(event.text);
    const QString target = QString::fromStdString(event.target);

    auto statusBuf = [this] { return findBuffer(statusKey()); };
    auto channelBuf = [this](const QString& name) -> Buffer& {
        return ensureBuffer(ChatView::Kind::Channel, name, bufferKeyFor(name));
    };

    switch (event.type) {
        case IrcEventType::Message:
        case IrcEventType::Action: {
            const bool action = event.type == IrcEventType::Action;
            Buffer* buf = nullptr;
            if (target.isEmpty()) {
                buf = statusBuf();
            } else if (m_backend->session() && m_backend->session()->isupport().chanTypes.find(
                                                   event.target.empty() ? '#' : event.target.front()) !=
                                                   std::string::npos &&
                       !target.isEmpty() &&
                       QStringLiteral("#&+!").contains(target.front())) {
                buf = &channelBuf(target);
            } else {
                // a query: create on demand (incoming DM) keyed by the peer
                buf = &ensureBuffer(ChatView::Kind::Query, target, bufferKeyFor(target));
            }
            buf->view->addMessage(ts, nick, text, action, event.self, event.raw.tag("+draft/channel-context").has_value());
            break;
        }
        case IrcEventType::Notice: {
            Buffer* buf = target.isEmpty() ? statusBuf() : findBuffer(bufferKeyFor(target));
            if (!buf) buf = statusBuf();
            buf->view->addMessage(ts, nick + QStringLiteral(" (notice)"), text, false, event.self);
            break;
        }
        case IrcEventType::Join: {
            Buffer& buf = channelBuf(target);
            if (event.self) buf.view->addSystemLine(tr("Now talking in %1").arg(target).toUtf8());
            else buf.view->addSystemLine(tr("%1 (%2@%3) joined")
                                             .arg(nick, QString::fromStdString(event.user),
                                                  QString::fromStdString(event.host)).toUtf8());
            refreshNicklist(target);
            break;
        }
        case IrcEventType::Part: {
            if (Buffer* b = findBuffer(bufferKeyFor(target))) {
                b->view->addSystemLine(tr("%1 left%2").arg(nick, text.isEmpty() ? QString()
                                            : QStringLiteral(" (%1)").arg(QString::fromUtf8(text))).toUtf8());
            }
            refreshNicklist(target);
            break;
        }
        case IrcEventType::Quit: {
            if (Buffer* b = findBuffer(bufferKeyFor(target))) {
                b->view->addSystemLine(tr("%1 quit%2").arg(nick, text.isEmpty() ? QString()
                                            : QStringLiteral(" (%1)").arg(QString::fromUtf8(text))).toUtf8());
            }
            refreshNicklist(target);
            break;
        }
        case IrcEventType::NickChange: {
            if (!target.isEmpty()) {
                if (Buffer* b = findBuffer(bufferKeyFor(target))) {
                    b->view->addSystemLine(tr("%1 is now known as %2")
                                               .arg(nick, QString::fromStdString(event.arg)).toUtf8());
                }
                refreshNicklist(target);
            }
            break;
        }
        case IrcEventType::Kick: {
            if (Buffer* b = findBuffer(bufferKeyFor(target))) {
                b->view->addSystemLine(tr("%1 was kicked by %2%3")
                                           .arg(QString::fromStdString(event.arg), nick,
                                                text.isEmpty() ? QString()
                                                               : QStringLiteral(" (%1)").arg(QString::fromUtf8(text)))
                                           .toUtf8());
            }
            refreshNicklist(target);
            break;
        }
        case IrcEventType::Mode: {
            Buffer* b = target.isEmpty() ? statusBuf() : findBuffer(bufferKeyFor(target));
            if (b) b->view->addSystemLine(tr("%1 set mode %2").arg(nick, QString::fromUtf8(text)).toUtf8());
            if (!target.isEmpty()) refreshNicklist(target);
            break;
        }
        case IrcEventType::Topic: {
            Buffer& b = channelBuf(target);
            b.view->setTopic(text);
            b.view->addSystemLine(tr("%1 changed the topic").arg(nick).toUtf8());
            break;
        }
        case IrcEventType::TopicState: {
            if (Buffer* b = findBuffer(bufferKeyFor(target))) b->view->setTopic(text);
            break;
        }
        case IrcEventType::NamesUpdated:
        case IrcEventType::MemberUpdated:
            refreshNicklist(target);
            break;
        case IrcEventType::Invite:
            if (Buffer* s = statusBuf())
                s->view->addSystemLine(tr("%1 invites you to %2").arg(nick, QString::fromStdString(event.arg)).toUtf8());
            break;
        case IrcEventType::Registered:
            if (Buffer* s = statusBuf()) s->view->addSystemLine(tr("Registered as %1").arg(nick).toUtf8());
            break;
        case IrcEventType::CtcpRequest:
        case IrcEventType::CtcpReply:
            if (Buffer* s = statusBuf())
                s->view->addSystemLine(tr("CTCP %1 %2 %3")
                                           .arg(event.type == IrcEventType::CtcpReply ? tr("reply") : tr("request"),
                                                QString::fromStdString(event.arg), QString::fromUtf8(text))
                                           .toUtf8());
            break;
        case IrcEventType::MotdLine:
        case IrcEventType::ServerText:
        case IrcEventType::AwaySelf:
            if (Buffer* s = statusBuf()) s->view->addMessage(ts, QString(), text);
            break;
        case IrcEventType::ErrorText:
        case IrcEventType::SaslFail:
            if (Buffer* s = statusBuf()) s->view->addSystemLine(QByteArray("⚠ ") + text);
            break;
        default:
            break;
    }
}

void MainWindow::onInput(Buffer* buffer, const QString& text) {
    if (!buffer) return;
    lurker::ParseContext ctx;
    ctx.kind = buffer->kind == ChatView::Kind::Channel  ? lurker::BufferKind::Channel
               : buffer->kind == ChatView::Kind::Query  ? lurker::BufferKind::Dm
                                                        : lurker::BufferKind::Server;
    ctx.target = buffer->display.toStdString();
    if (m_backend && m_backend->session()) {
        const auto& ct = m_backend->session()->isupport().chanTypes;
        if (!ct.empty()) ctx.chanTypes = ct;
    }

    const lurker::ParseResult result = lurker::parseInput(text.toStdString(), ctx);
    if (!result.ok()) {
        buffer->view->addSystemLine(QByteArray("⚠ ") + QByteArray::fromStdString(result.error));
        return;
    }
    if (!m_backend || !m_backend->session() || !m_backend->isRegistered()) {
        buffer->view->addSystemLine(tr("Not connected.").toUtf8());
        return;
    }
    lurker::IrcSession* s = m_backend->session();
    for (const lurker::Action& a : result.actions) {
        using lurker::ActionKind;
        switch (a.kind) {
            case ActionKind::Send: s->sendMessage(a.target, a.text); break;
            case ActionKind::Action: s->sendAction(a.target, a.text); break;
            case ActionKind::Notice: s->sendNotice(a.target, a.text); break;
            case ActionKind::Raw: s->sendRaw(a.line); break;
            case ActionKind::Join: s->join(a.channel, a.key); break;
            case ActionKind::Part: s->part(a.channel, a.reason); break;
            case ActionKind::SetTopic: s->setTopic(a.channel, a.text); break;
            case ActionKind::Away: s->setAway(a.text); break;
            case ActionKind::Back: s->clearAway(); break;
            case ActionKind::Ctcp: s->sendCtcpRequest(a.target, a.ctcpType, a.text); break;
            case ActionKind::ListChannels: s->sendRaw("LIST"); break;
            case ActionKind::OpenBuffer: {
                Buffer& q = ensureBuffer(ChatView::Kind::Query, QString::fromStdString(a.target),
                                         bufferKeyFor(QString::fromStdString(a.target)));
                m_mdi->setActiveSubWindow(q.sub);
                q.view->focusInput();
                break;
            }
            case ActionKind::CloseBuffer: closeBuffer(buffer); break;
            case ActionKind::LocalClear: /* handled below */ break;
            case ActionKind::LocalHelp:
                buffer->view->addSystemLine(QByteArray::fromStdString(lurker::helpText()));
                break;
        }
        if (a.kind == ActionKind::LocalClear) {
            // Rebuild the view fresh is overkill; a cleared marker suffices for now.
            buffer->view->addSystemLine(tr("(cleared)").toUtf8());
        }
    }
}

void MainWindow::showConnectDialog() {
    ConnectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    connectWith(dialog.settings());
}

void MainWindow::connectWith(const lurker::backend::ConnectSettings& settings) {
    m_backend->connectToHost(settings);
    if (Buffer* s = findBuffer(statusKey())) {
        m_mdi->setActiveSubWindow(s->sub);
        s->view->focusInput();
    }
}

}  // namespace lurker::gui
