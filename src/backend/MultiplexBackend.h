// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// MultiplexBackend presents several ChatBackend connections as one, so the GUI
// can run a Lurker connection and one or more direct-IRC connections at the same
// time (issue #3). The GUI is already multi-network (Lurker hands it several
// networks over one socket); the only new problem is that two child backends can
// use the same local network id. The multiplexer gives each child a disjoint
// slice of the id space and translates in both directions:
//   - outward (child -> GUI): every net-carrying signal is re-emitted with the
//     network id mapped to the global id (globalNet).
//   - inward  (GUI -> child): every net-taking call is routed to the owning
//     child with the id mapped back to that child's local id.
// Child 0 (the first connection, normally Lurker) keeps its ids unchanged, so a
// single-connection setup behaves exactly as before.
#pragma once

#include <QList>

#include "backend/ChatBackend.h"

namespace lurker::backend {

class MultiplexBackend : public ChatBackend {
    Q_OBJECT
public:
    explicit MultiplexBackend(QObject* parent = nullptr);

    // Adopt a child connection (takes ownership) and wire its signals through.
    void addChild(ChatBackend* child);
    int childCount() const { return static_cast<int>(m_children.size()); }

    // Map a child's local network id to the global id the GUI sees, and back.
    // Negative ids (status/unknown) are shared and never offset.
    int globalNet(int childIndex, int localNet) const {
        return localNet < 0 ? localNet : childIndex * kStride + localNet;
    }

    bool isReady() const override;  // ready if any child is
    void sendMessage(int net, const QString& target, const QString& text) override;
    void sendAction(int net, const QString& target, const QString& text) override;
    void sendNotice(int net, const QString& target, const QString& text) override;
    void sendRaw(int net, const QString& line) override;
    void joinChannel(int net, const QString& channel, const QString& key) override;
    void partChannel(int net, const QString& channel, const QString& reason) override;
    void setTopic(int net, const QString& channel, const QString& topic) override;
    void setAway(int net, const QString& message) override;
    void clearAway(int net) override;
    void sendCtcp(int net, const QString& target, const QString& type, const QString& args) override;
    void requestHistory(int net, const QString& target) override;
    void markRead(int net, const QString& target, qlonglong messageId) override;
    void disconnectAll() override;  // disconnects every child
    void requestChannelList(int net) override;

    // Capabilities are the union of the children's; the specific call is routed
    // to the owning child (by net) or to the first capable child (DCC/previews).
    bool supportsDccChat() const override;
    void startDccChat(int net, const QString& nick) override;
    bool supportsPreviews() const override;
    QString previewBase() const override;
    QString authToken() const override;
    bool supportsDccTransfers() const override;
    void requestDccTransfers() override;
    void acceptDcc(int id) override;
    void rejectDcc(int id) override;
    void cancelDcc(int id) override;
    void deleteDccFile(int id) override;
    void saveDccFile(int id, const QString& path) override;
    bool supportsFileSend() const override;
    void sendFile(int net, const QString& target, const QString& localPath) override;

private:
    static constexpr int kStride = 1000000;  // network-id slice per child

    // Resolve a global net id to its child and that child's local id. Returns
    // nullptr if no child owns it; a negative net resolves to the first child.
    ChatBackend* childForNet(int globalNet, int& localNet) const;
    ChatBackend* previewChild() const;  // first child that supports previews
    ChatBackend* dccChild() const;      // first child that supports DCC transfers

    QList<ChatBackend*> m_children;
};

}  // namespace lurker::backend
