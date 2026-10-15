// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "backend/MultiplexBackend.h"

namespace lurker::backend {

MultiplexBackend::MultiplexBackend(QObject* parent) : ChatBackend(parent) {}

void MultiplexBackend::addChild(ChatBackend* child) {
    if (!child) return;
    const int idx = static_cast<int>(m_children.size());
    m_children.append(child);
    child->setParent(this);

    // Signals without a network id pass straight through.
    connect(child, &ChatBackend::statusText, this, &ChatBackend::statusText);
    connect(child, &ChatBackend::readyChanged, this, &ChatBackend::readyChanged);
    connect(child, &ChatBackend::closed, this, &ChatBackend::closed);
    connect(child, &ChatBackend::fileUploadProgress, this, &ChatBackend::fileUploadProgress);
    connect(child, &ChatBackend::fileUploadError, this, &ChatBackend::fileUploadError);
    connect(child, &ChatBackend::dccFileSaved, this, &ChatBackend::dccFileSaved);

    // Signals carrying a network id are re-emitted with the id mapped to global.
    connect(child, &ChatBackend::event, this, [this, idx](const lurker::IrcEvent& ev) {
        lurker::IrcEvent e = ev;
        e.networkId = globalNet(idx, ev.networkId);
        emit event(e);
    });
    connect(child, &ChatBackend::bufferOpened, this, [this, idx](int net, const QString& target, int kind) {
        emit bufferOpened(globalNet(idx, net), target, kind);
    });
    connect(child, &ChatBackend::membersReplaced, this,
            [this, idx](int net, const QString& target, const QList<DisplayMember>& members) {
                emit membersReplaced(globalNet(idx, net), target, members);
            });
    connect(child, &ChatBackend::topicChanged, this,
            [this, idx](int net, const QString& target, const QByteArray& topic) {
                emit topicChanged(globalNet(idx, net), target, topic);
            });
    connect(child, &ChatBackend::readStateChanged, this,
            [this, idx](int net, const QString& target, int unread, int highlights) {
                emit readStateChanged(globalNet(idx, net), target, unread, highlights);
            });
    connect(child, &ChatBackend::reactionUpdated, this,
            [this, idx](int net, const QString& target, qlonglong id, const QString& nick, const QString& value,
                        bool self, bool remove) {
                emit reactionUpdated(globalNet(idx, net), target, id, nick, value, self, remove);
            });
    connect(child, &ChatBackend::networkUpdated, this, [this, idx](const NetworkInfo& info) {
        NetworkInfo g = info;
        g.id = globalNet(idx, info.id);
        emit networkUpdated(g);
    });
    connect(child, &ChatBackend::fileUploaded, this,
            [this, idx](int net, const QString& target, const QString& name, const QString& url) {
                emit fileUploaded(globalNet(idx, net), target, name, url);
            });
    connect(child, &ChatBackend::dccTransferUpdated, this, [this, idx](const DccTransfer& t) {
        DccTransfer g = t;
        g.networkId = globalNet(idx, t.networkId);
        emit dccTransferUpdated(g);
    });
    connect(child, &ChatBackend::dccTransferList, this, [this, idx](const QList<DccTransfer>& list) {
        QList<DccTransfer> g;
        g.reserve(list.size());
        for (DccTransfer t : list) {
            t.networkId = globalNet(idx, t.networkId);
            g.append(t);
        }
        emit dccTransferList(g);
    });

    // A late-joining child may already be ready; nudge the GUI to re-query.
    emit readyChanged();
}

ChatBackend* MultiplexBackend::childForNet(int globalNet, int& localNet) const {
    if (m_children.isEmpty()) {
        localNet = globalNet;
        return nullptr;
    }
    if (globalNet < 0) {  // status / unknown — belongs to the primary connection
        localNet = globalNet;
        return m_children.front();
    }
    const int idx = globalNet / kStride;
    localNet = globalNet % kStride;
    return (idx >= 0 && idx < static_cast<int>(m_children.size())) ? m_children[idx] : nullptr;
}

ChatBackend* MultiplexBackend::previewChild() const {
    for (ChatBackend* c : m_children)
        if (c->supportsPreviews()) return c;
    return nullptr;
}

ChatBackend* MultiplexBackend::dccChild() const {
    for (ChatBackend* c : m_children)
        if (c->supportsDccTransfers()) return c;
    return nullptr;
}

ChatBackend* MultiplexBackend::reactsChild() const {
    for (ChatBackend* c : m_children)
        if (c->supportsReactions()) return c;
    return nullptr;
}

bool MultiplexBackend::isReady() const {
    for (ChatBackend* c : m_children)
        if (c->isReady()) return true;
    return false;
}

// --- net-routed calls ----------------------------------------------------

void MultiplexBackend::sendMessage(int net, const QString& target, const QString& text) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->sendMessage(ln, target, text);
}
void MultiplexBackend::sendAction(int net, const QString& target, const QString& text) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->sendAction(ln, target, text);
}
void MultiplexBackend::sendNotice(int net, const QString& target, const QString& text) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->sendNotice(ln, target, text);
}
void MultiplexBackend::sendRaw(int net, const QString& line) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->sendRaw(ln, line);
}
void MultiplexBackend::joinChannel(int net, const QString& channel, const QString& key) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->joinChannel(ln, channel, key);
}
void MultiplexBackend::partChannel(int net, const QString& channel, const QString& reason) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->partChannel(ln, channel, reason);
}
void MultiplexBackend::setTopic(int net, const QString& channel, const QString& topic) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->setTopic(ln, channel, topic);
}
void MultiplexBackend::setAway(int net, const QString& message) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->setAway(ln, message);
}
void MultiplexBackend::clearAway(int net) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->clearAway(ln);
}
void MultiplexBackend::sendCtcp(int net, const QString& target, const QString& type, const QString& args) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->sendCtcp(ln, target, type, args);
}
void MultiplexBackend::requestHistory(int net, const QString& target) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->requestHistory(ln, target);
}
void MultiplexBackend::markRead(int net, const QString& target, qlonglong messageId) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->markRead(ln, target, messageId);
}
void MultiplexBackend::requestChannelList(int net) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->requestChannelList(ln);
}
void MultiplexBackend::startDccChat(int net, const QString& nick) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->startDccChat(ln, nick);
}
void MultiplexBackend::sendFile(int net, const QString& target, const QString& localPath) {
    int ln;
    if (ChatBackend* c = childForNet(net, ln)) c->sendFile(ln, target, localPath);
}

void MultiplexBackend::disconnectAll() {
    for (ChatBackend* c : m_children) c->disconnectAll();
}

void MultiplexBackend::disconnectNetwork(int globalNet) {
    int ln;
    if (ChatBackend* c = childForNet(globalNet, ln)) c->disconnectAll();
}

// --- capability / DCC-by-id (routed to the owning child) -----------------

bool MultiplexBackend::supportsDccChat() const {
    for (ChatBackend* c : m_children)
        if (c->supportsDccChat()) return true;
    return false;
}
bool MultiplexBackend::supportsPreviews() const { return previewChild() != nullptr; }
QString MultiplexBackend::previewBase() const {
    ChatBackend* c = previewChild();
    return c ? c->previewBase() : QString();
}
QString MultiplexBackend::authToken() const {
    ChatBackend* c = previewChild();
    return c ? c->authToken() : QString();
}
bool MultiplexBackend::supportsDccTransfers() const { return dccChild() != nullptr; }
void MultiplexBackend::requestDccTransfers() {
    if (ChatBackend* c = dccChild()) c->requestDccTransfers();
}
void MultiplexBackend::acceptDcc(int id) {
    if (ChatBackend* c = dccChild()) c->acceptDcc(id);
}
void MultiplexBackend::rejectDcc(int id) {
    if (ChatBackend* c = dccChild()) c->rejectDcc(id);
}
void MultiplexBackend::cancelDcc(int id) {
    if (ChatBackend* c = dccChild()) c->cancelDcc(id);
}
void MultiplexBackend::deleteDccFile(int id) {
    if (ChatBackend* c = dccChild()) c->deleteDccFile(id);
}
void MultiplexBackend::saveDccFile(int id, const QString& path) {
    if (ChatBackend* c = dccChild()) c->saveDccFile(id, path);
}
bool MultiplexBackend::supportsFileSend() const {
    for (ChatBackend* c : m_children)
        if (c->supportsFileSend()) return true;
    return false;
}
bool MultiplexBackend::supportsReactions() const { return reactsChild() != nullptr; }
void MultiplexBackend::react(qlonglong messageId, const QString& value, bool remove) {
    if (ChatBackend* c = reactsChild()) c->react(messageId, value, remove);
}

}  // namespace lurker::backend
