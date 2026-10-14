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
    std::function<void(const QString&, const lurker::msl::DialogTable&, bool)> openDialogFn;
    std::function<void(const QString&)> closeDialogFn;
    std::function<void(const QString&, int, const QString&, const QString&)> didCommandFn;
    std::function<QString(const QString&, int, const QString&)> didQueryFn;

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
    void openDialog(const std::string& instance, const lurker::msl::DialogTable& table, bool modal) override {
        if (openDialogFn) openDialogFn(QString::fromStdString(instance), table, modal);
    }
    void closeDialog(const std::string& instance) override {
        if (closeDialogFn) closeDialogFn(QString::fromStdString(instance));
    }
    void didCommand(const std::string& instance, int id, const std::string& sw, const std::string& text) override {
        if (didCommandFn)
            didCommandFn(QString::fromStdString(instance), id, QString::fromStdString(sw), QString::fromStdString(text));
    }
    std::string didQuery(const std::string& instance, int id, const std::string& prop) const override {
        return didQueryFn ? didQueryFn(QString::fromStdString(instance), id, QString::fromStdString(prop)).toStdString()
                          : std::string();
    }
};

}  // namespace lurker::gui
