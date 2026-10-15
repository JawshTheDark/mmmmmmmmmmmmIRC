// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Unit test for MultiplexBackend: network-id namespacing (outward signals) and
// routing (inward calls) across several child backends. No network needed.

#include <QCoreApplication>

#include "backend/ChatBackend.h"
#include "backend/MultiplexBackend.h"
#include "check.h"
#include "core/IrcEvent.h"

using namespace lurker::backend;

namespace {
// A ChatBackend that records the last inward call and can fire outward signals.
class MockBackend : public ChatBackend {
public:
    bool ready = false;
    int lastNet = -999;
    QString lastTarget, lastText, lastRaw;

    bool isReady() const override { return ready; }
    void sendMessage(int net, const QString& target, const QString& text) override {
        lastNet = net;
        lastTarget = target;
        lastText = text;
    }
    void sendAction(int, const QString&, const QString&) override {}
    void sendNotice(int, const QString&, const QString&) override {}
    void sendRaw(int net, const QString& line) override {
        lastNet = net;
        lastRaw = line;
    }
    void joinChannel(int net, const QString& channel, const QString&) override {
        lastNet = net;
        lastTarget = channel;
    }
    void partChannel(int, const QString&, const QString&) override {}
    void setTopic(int, const QString&, const QString&) override {}
    void setAway(int, const QString&) override {}
    void clearAway(int) override {}
    void sendCtcp(int, const QString&, const QString&, const QString&) override {}
    void requestHistory(int, const QString&) override {}
    void markRead(int, const QString&, qlonglong) override {}
    void disconnectAll() override { ready = false; }

    // test helpers — fire outward signals as the real adapters would
    void fireEvent(int net, const QString& nick, const QString& target) {
        lurker::IrcEvent ev;
        ev.type = lurker::IrcEventType::Message;
        ev.networkId = net;
        ev.nick = nick.toStdString();
        ev.target = target.toStdString();
        emit event(ev);
    }
    void fireBufferOpened(int net, const QString& target) { emit bufferOpened(net, target, 0); }
};
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // QObject signals need an application object

    MultiplexBackend mux;
    auto* a = new MockBackend;  // child 0 — e.g. the Lurker connection
    auto* b = new MockBackend;  // child 1 — e.g. a direct IRC network

    int gotNet = -999;
    QObject::connect(&mux, &ChatBackend::event, [&](const lurker::IrcEvent& ev) { gotNet = ev.networkId; });
    int boNet = -999;
    QString boTarget;
    QObject::connect(&mux, &ChatBackend::bufferOpened, [&](int net, const QString& t, int) {
        boNet = net;
        boTarget = t;
    });

    mux.addChild(a);
    mux.addChild(b);
    CHECK_EQ(mux.childCount(), 2);

    // Outward: child 0 keeps its ids; child 1 is offset by the stride; a negative
    // (status/unknown) id is shared and never offset.
    a->fireEvent(2, "alice", "#chan");
    CHECK_EQ(gotNet, 2);
    b->fireEvent(2, "bob", "#chan");
    CHECK_EQ(gotNet, 1000002);
    a->fireEvent(6, "carol", "#x");
    CHECK_EQ(gotNet, 6);
    b->fireEvent(-1, "server", "");
    CHECK_EQ(gotNet, -1);

    // Inward: a global id routes to the owning child with its local id restored.
    mux.sendMessage(2, "#chan", "to-a");
    CHECK_EQ(a->lastNet, 2);
    CHECK(a->lastText == QStringLiteral("to-a"));
    mux.sendMessage(1000002, "#chan", "to-b");
    CHECK_EQ(b->lastNet, 2);
    CHECK(b->lastText == QStringLiteral("to-b"));
    CHECK(a->lastText == QStringLiteral("to-a"));  // child a untouched by the b-routed call

    mux.sendRaw(1000005, "LIST");
    CHECK_EQ(b->lastNet, 5);
    CHECK(b->lastRaw == QStringLiteral("LIST"));

    // Unknown/status id routes to the primary child.
    mux.joinChannel(-1, "#s", QString());
    CHECK_EQ(a->lastNet, -1);

    // Out-of-range global id resolves to no child (no crash, no misroute).
    b->lastNet = -777;
    mux.sendMessage(9 * 1000000 + 1, "#none", "x");
    CHECK_EQ(b->lastNet, -777);

    // isReady is the union of the children's readiness.
    CHECK(!mux.isReady());
    b->ready = true;
    CHECK(mux.isReady());

    // bufferOpened is remapped like event.
    b->fireBufferOpened(3, "#room");
    CHECK_EQ(boNet, 1000003);
    CHECK(boTarget == QStringLiteral("#room"));

    return check::summary();
}
