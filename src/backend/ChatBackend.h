// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ChatBackend is the backend-agnostic interface the GUI drives. Two adapters
// implement it: BouncerChatBackend (raw IRC, one network) and LurkerChatBackend
// (Lurker WS/REST, every network at once). The GUI knows only this interface,
// so one set of windows renders both. Buffer creation, messages, membership and
// topics all arrive as the signals below, each tagged with a networkId.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "core/IrcEvent.h"

namespace lurker::backend {

struct NetworkInfo {
    int id = -1;
    QString name;   // display name ("Libera.Chat")
    QString state;  // "connecting" | "connected" | "disconnected" | ...
    QString nick;   // our nick on this network
};

// A nicklist entry ready to show: the adapter has already resolved the rank
// sigil from the network's PREFIX/modeSpec and sorted the list.
struct DisplayMember {
    QString sigil;  // "@", "+", "" ...
    QString nick;
    bool away = false;
};

enum class BufferKind { Channel = 0, Dm = 1, Server = 2 };

// A DCC transfer row as surfaced to the UI (from Lurker's /api/dcc + the live
// `dcc-transfer` frames). Mirrors the server's row; only the fields the Transfers
// window needs are kept.
struct DccTransfer {
    int id = 0;
    int networkId = -1;
    QString peer;         // peer_nick
    QString direction;    // "incoming" | "outgoing"
    QString filename;
    qlonglong size = 0;   // advertised_size
    qlonglong received = 0;  // received_bytes
    QString state;        // requested | pending_approval | connecting | receiving | stalled | verifying | completed | failed | rejected | cancelled
    QString error;
    bool fileDeleted = false;
    // Active (non-terminal) states can still be cancelled/rejected.
    bool active() const {
        return state == QLatin1String("requested") || state == QLatin1String("pending_approval") ||
               state == QLatin1String("connecting") || state == QLatin1String("receiving") ||
               state == QLatin1String("stalled") || state == QLatin1String("verifying");
    }
    bool awaitingApproval() const {
        return state == QLatin1String("requested") || state == QLatin1String("pending_approval");
    }
    bool completed() const { return state == QLatin1String("completed"); }
};

class ChatBackend : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    ~ChatBackend() override = default;

    virtual bool isReady() const = 0;  // registered / connected enough to send

    // Client -> server. `net` selects the network; adapters that hold a single
    // network ignore it.
    virtual void sendMessage(int net, const QString& target, const QString& text) = 0;
    virtual void sendAction(int net, const QString& target, const QString& text) = 0;
    virtual void sendNotice(int net, const QString& target, const QString& text) = 0;
    virtual void sendRaw(int net, const QString& line) = 0;
    virtual void joinChannel(int net, const QString& channel, const QString& key) = 0;
    virtual void partChannel(int net, const QString& channel, const QString& reason) = 0;
    virtual void setTopic(int net, const QString& channel, const QString& topic) = 0;
    virtual void setAway(int net, const QString& message) = 0;
    virtual void clearAway(int net) = 0;
    virtual void sendCtcp(int net, const QString& target, const QString& type, const QString& args) = 0;
    virtual void requestHistory(int net, const QString& target) = 0;
    virtual void markRead(int net, const QString& target, qlonglong messageId) = 0;
    virtual void disconnectAll() = 0;

    // IRCv3 reactions (+draft/react). Default: unsupported (raw bouncer has no
    // stored message ids to react to). `messageId` is the persisted line's id.
    virtual bool supportsReactions() const { return false; }
    virtual void react(qlonglong messageId, const QString& value, bool remove) {
        (void)messageId;
        (void)value;
        (void)remove;
    }

    // Ask the network for its channel list. Default is a raw /LIST, whose
    // 321/322/323 numerics the GUI already routes to the Channels List window —
    // correct for real IRC (the bouncer). Backends with a structured
    // channel-list API (Lurker caches /LIST server-side and serves it over
    // dedicated frames) override this and synthesise the same numerics.
    virtual void requestChannelList(int net) { sendRaw(net, QStringLiteral("LIST")); }

    // DCC chat proxied through Lurker (default: unsupported, e.g. raw bouncer).
    virtual bool supportsDccChat() const { return false; }
    virtual void startDccChat(int networkId, const QString& nick) {
        (void)networkId;
        (void)nick;
    }

    // Link/media previews through Lurker's previews proxy (default: unsupported).
    // previewBase is the REST origin (https://host); authToken is the Bearer.
    virtual bool supportsPreviews() const { return false; }
    virtual QString previewBase() const { return {}; }
    virtual QString authToken() const { return {}; }

    // DCC file transfers proxied through Lurker (/api/dcc): list, accept/reject a
    // pending offer, cancel an in-flight one, save a completed file locally, or
    // delete the server copy. Default: unsupported (raw bouncer).
    virtual bool supportsDccTransfers() const { return false; }
    virtual void requestDccTransfers() {}
    virtual void acceptDcc(int id) { (void)id; }
    virtual void rejectDcc(int id) { (void)id; }
    virtual void cancelDcc(int id) { (void)id; }
    virtual void deleteDccFile(int id) { (void)id; }
    virtual void saveDccFile(int id, const QString& path) {
        (void)id;
        (void)path;
    }

    // Outgoing file send: upload a local file through Lurker (the bouncer's file
    // host) and share the resulting URL with `target`. Default: unsupported.
    virtual bool supportsFileSend() const { return false; }
    virtual void sendFile(int net, const QString& target, const QString& localPath) {
        (void)net;
        (void)target;
        (void)localPath;
    }

signals:
    void statusText(const QString& text);
    void networkUpdated(const lurker::backend::NetworkInfo& net);
    void bufferOpened(int net, const QString& target, int kind);  // kind = BufferKind
    void event(const lurker::IrcEvent& ev);                        // one line to append
    void membersReplaced(int net, const QString& target, const QList<lurker::backend::DisplayMember>& members);
    void topicChanged(int net, const QString& target, const QByteArray& topic);
    void readStateChanged(int net, const QString& target, int unread, int highlights);
    // One IRCv3 reaction was added (or, with remove, taken back) on a stored line.
    void reactionUpdated(int net, const QString& target, qlonglong messageId, const QString& nick,
                         const QString& value, bool self, bool remove);
    void readyChanged();
    void closed();
    // DCC: the full list (initial load) and single-row live updates.
    void dccTransferList(const QList<lurker::backend::DccTransfer>& transfers);
    void dccTransferUpdated(const lurker::backend::DccTransfer& transfer);
    void dccFileSaved(int id, const QString& path);
    // Outgoing file upload (send): progress, completion (with the shared URL), error.
    void fileUploadProgress(const QString& name, int percent);
    void fileUploaded(int net, const QString& target, const QString& name, const QString& url);
    void fileUploadError(const QString& name, const QString& error);
};

}  // namespace lurker::backend
