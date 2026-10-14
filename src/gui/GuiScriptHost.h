// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Bridges the Qt-free MSL engine to the GUI: /echo lands in a window, sends go
// to the active backend, and the active target follows the focused buffer.
// MainWindow wires the callbacks; the engine stays decoupled from Qt.
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    std::function<QString(const QString&, int, double, const QString&)> startTimerFn;
    std::function<void(const QString&)> stopTimerFn;
    std::function<void(const QString&, const QString&)> openWindowFn;
    std::function<void(const QString&)> clearWindowFn;
    std::function<void(const QString&, const QString&, const QStringList&)> drawOpFn;

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
    std::string startTimer(const std::string& name, int repeat, double interval, const std::string& cmd) override {
        return startTimerFn ? startTimerFn(QString::fromStdString(name), repeat, interval, QString::fromStdString(cmd))
                                  .toStdString()
                            : std::string();
    }
    void stopTimer(const std::string& name) override {
        if (stopTimerFn) stopTimerFn(QString::fromStdString(name));
    }
    void openWindow(const std::string& name, const std::string& sw) override {
        if (openWindowFn) openWindowFn(QString::fromStdString(name), QString::fromStdString(sw));
    }
    void clearWindow(const std::string& name) override {
        if (clearWindowFn) clearWindowFn(QString::fromStdString(name));
    }
    void drawOp(const std::string& window, const std::string& op, const std::vector<std::string>& args) override {
        if (!drawOpFn) return;
        QStringList qargs;
        for (const auto& s : args) qargs << QString::fromStdString(s);
        drawOpFn(QString::fromStdString(window), QString::fromStdString(op), qargs);
    }
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

    // Filesystem: relative paths resolve against baseDir (the script/config dir).
    QString baseDir;
    QString resolve(const std::string& p) const {
        const QString path = QString::fromStdString(p);
        return QFileInfo(path).isAbsolute() || baseDir.isEmpty() ? path : QDir(baseDir).filePath(path);
    }
    std::string fileRead(const std::string& p) const override {
        QFile f(resolve(p));
        return f.open(QIODevice::ReadOnly) ? f.readAll().toStdString() : std::string();
    }
    bool fileWrite(const std::string& p, const std::string& content) override {
        QFile f(resolve(p));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(content.data(), static_cast<qint64>(content.size()));
        return true;
    }
    bool fileExists(const std::string& p) const override { return QFileInfo(resolve(p)).isFile(); }
    bool dirExists(const std::string& p) const override { return QFileInfo(resolve(p)).isDir(); }
    long long fileSize(const std::string& p) const override {
        QFileInfo fi(resolve(p));
        return fi.isFile() ? fi.size() : -1;
    }
    bool makeDir(const std::string& p) override { return QDir().mkpath(resolve(p)); }
    bool removePath(const std::string& p) override { return QFile::remove(resolve(p)); }
    std::vector<std::string> listDir(const std::string& dir, const std::string& wildcard) const override {
        std::vector<std::string> out;
        for (const QString& name :
             QDir(resolve(dir)).entryList({QString::fromStdString(wildcard)}, QDir::Files, QDir::Name))
            out.push_back(name.toStdString());
        return out;
    }
};

}  // namespace lurker::gui
