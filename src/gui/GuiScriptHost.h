// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Bridges the Qt-free MSL engine to the GUI: /echo lands in a window, sends go
// to the active backend, and the active target follows the focused buffer.
// MainWindow wires the callbacks; the engine stays decoupled from Qt.
#pragma once

#include <QString>
#include <ctime>
#include <functional>

#include "core/msl/Host.h"

namespace lurker::gui {

class GuiScriptHost : public lurker::msl::Host {
public:
    std::function<void(const lurker::msl::ScriptTarget&, const QString&)> echoFn;
    std::function<void(int, const QString&, const QString&)> messageFn;
    std::function<void(int, const QString&, const QString&)> actionFn;
    std::function<void(int, const QString&)> rawFn;
    std::function<lurker::msl::ScriptTarget()> activeFn;
    std::function<QString(int)> meFn;
    std::function<void(const QString&)> errorFn;

    void echo(const lurker::msl::ScriptTarget& where, const std::string& text) override {
        if (echoFn) echoFn(where, QString::fromStdString(text));
    }
    void sendMessage(int net, const std::string& target, const std::string& text) override {
        if (messageFn) messageFn(net, QString::fromStdString(target), QString::fromStdString(text));
    }
    void sendAction(int net, const std::string& target, const std::string& text) override {
        if (actionFn) actionFn(net, QString::fromStdString(target), QString::fromStdString(text));
    }
    void sendRaw(int net, const std::string& line) override {
        if (rawFn) rawFn(net, QString::fromStdString(line));
    }
    lurker::msl::ScriptTarget activeTarget() const override {
        return activeFn ? activeFn() : lurker::msl::ScriptTarget{};
    }
    std::string me(int net) const override { return meFn ? meFn(net).toStdString() : std::string(); }
    long long unixTime() const override { return static_cast<long long>(::time(nullptr)); }
    std::string startTimer(const std::string&, int, double, const std::string&) override { return {}; }
    void stopTimer(const std::string&) override {}
    void scriptError(const std::string& text) override {
        if (errorFn) errorFn(QString::fromStdString(text));
    }
};

}  // namespace lurker::gui
