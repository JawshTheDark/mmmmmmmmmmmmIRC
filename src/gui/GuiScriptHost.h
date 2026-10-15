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
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QObject>
#include <QStandardPaths>
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
    std::function<QString(const QString&, const QString&, const QString&)> dllCallFn;  // /dll + $dll
    std::function<QString(const QString&, int, double, const QString&)> startTimerFn;
    std::function<void(const QString&)> stopTimerFn;
    std::function<void(const QString&, const QString&)> openWindowFn;
    std::function<void(const QString&)> clearWindowFn;
    std::function<void(const QString&, int)> setBarVisibleFn;
    std::function<void(const QString&, const QString&, const QStringList&)> drawOpFn;
    std::function<void(const QString&, const QString&, int, bool)> sockOpenFn;
    std::function<void(const QString&, const QString&, bool)> sockWriteFn;
    std::function<void(const QString&)> sockCloseFn;
    std::function<QString(const QString&, bool&)> sockReadLineFn;
    std::function<QString(const QString&, const QString&)> sockInfoFn;
    std::function<QStringList(int, const QString&)> channelNicksFn;  // $nick(#,N)
    std::function<QStringList(int)> myChannelsFn;                    // $chan(N)
    std::function<bool(int, const QString&, const QString&)> isOnFn; // $comchan / $ison
    std::function<QString(int)> networkNameFn;                       // $network
    std::function<QString(int)> serverHostFn;                        // $server
    std::function<QStringList()> scriptFilesFn;                      // $script
    std::function<QStringList()> windowNamesFn;                      // $window
    std::function<int(const QString&)> windowLineCountFn;            // $line(win,0)
    std::function<QString(const QString&, int)> windowLineFn;        // $line(win,N)

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
    std::vector<std::string> channelNicks(int net, const std::string& chan) const override {
        std::vector<std::string> out;
        if (channelNicksFn)
            for (const QString& n : channelNicksFn(net, QString::fromStdString(chan))) out.push_back(n.toStdString());
        return out;
    }
    std::vector<std::string> myChannels(int net) const override {
        std::vector<std::string> out;
        if (myChannelsFn)
            for (const QString& c : myChannelsFn(net)) out.push_back(c.toStdString());
        return out;
    }
    bool isOn(int net, const std::string& chan, const std::string& nick) const override {
        return isOnFn ? isOnFn(net, QString::fromStdString(chan), QString::fromStdString(nick)) : false;
    }
    std::string networkName(int net) const override { return networkNameFn ? networkNameFn(net).toStdString() : std::string(); }
    std::string serverHost(int net) const override { return serverHostFn ? serverHostFn(net).toStdString() : std::string(); }
    std::vector<std::string> scriptFiles() const override {
        std::vector<std::string> out;
        if (scriptFilesFn)
            for (const QString& s : scriptFilesFn()) out.push_back(s.toStdString());
        return out;
    }
    std::vector<std::string> windowNames() const override {
        std::vector<std::string> out;
        if (windowNamesFn)
            for (const QString& s : windowNamesFn()) out.push_back(s.toStdString());
        return out;
    }
    int windowLineCount(const std::string& w) const override {
        return windowLineCountFn ? windowLineCountFn(QString::fromStdString(w)) : 0;
    }
    std::string windowLine(const std::string& w, int n) const override {
        return windowLineFn ? windowLineFn(QString::fromStdString(w), n).toStdString() : std::string();
    }
    long long unixTime() const override { return static_cast<long long>(::time(nullptr)); }
    std::string appDir() const override {
        const QString d = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        return d.isEmpty() ? std::string() : (d + QLatin1Char('/')).toStdString();
    }
    std::string scriptDir() const override {
        return baseDir.isEmpty() ? std::string() : (baseDir + QLatin1Char('/')).toStdString();
    }
    std::string startTimer(const std::string& name, int repeat, double interval, const std::string& cmd) override {
        return startTimerFn ? startTimerFn(QString::fromStdString(name), repeat, interval, QString::fromStdString(cmd))
                                  .toStdString()
                            : std::string();
    }
    void stopTimer(const std::string& name) override {
        if (stopTimerFn) stopTimerFn(QString::fromStdString(name));
    }
    void setBarVisible(const std::string& bar, int mode) override {
        if (setBarVisibleFn) setBarVisibleFn(QString::fromStdString(bar), mode);
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
    std::string inputDialog(const std::string& prompt, bool masked) override {
        bool ok = false;
        const QString text = QInputDialog::getText(nullptr, QObject::tr("Input"), QString::fromStdString(prompt),
                                                   masked ? QLineEdit::Password : QLineEdit::Normal, QString(), &ok);
        return ok ? text.toStdString() : std::string();
    }
    std::string chooseDir(const std::string& start, const std::string& title) override {
        return QFileDialog::getExistingDirectory(nullptr, QString::fromStdString(title), QString::fromStdString(start))
            .toStdString();
    }
    std::string chooseFile(const std::string& start, const std::string& title, const std::string& filter) override {
        return QFileDialog::getOpenFileName(nullptr, QString::fromStdString(title), QString::fromStdString(start),
                                            QString::fromStdString(filter))
            .toStdString();
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
    std::string dllCall(const std::string& file, const std::string& proc, const std::string& data) override {
        return dllCallFn ? dllCallFn(QString::fromStdString(file), QString::fromStdString(proc),
                                     QString::fromStdString(data))
                               .toStdString()
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

    void sockOpen(const std::string& name, const std::string& host, int port, bool ssl) override {
        if (sockOpenFn) sockOpenFn(QString::fromStdString(name), QString::fromStdString(host), port, ssl);
    }
    void sockWrite(const std::string& name, const std::string& data, bool nl) override {
        if (sockWriteFn) sockWriteFn(QString::fromStdString(name), QString::fromStdString(data), nl);
    }
    void sockClose(const std::string& name) override {
        if (sockCloseFn) sockCloseFn(QString::fromStdString(name));
    }
    std::string sockReadLine(const std::string& name, bool& ok) override {
        if (!sockReadLineFn) {
            ok = false;
            return {};
        }
        return sockReadLineFn(QString::fromStdString(name), ok).toStdString();
    }
    std::string sockInfo(const std::string& name, const std::string& prop) const override {
        return sockInfoFn ? sockInfoFn(QString::fromStdString(name), QString::fromStdString(prop)).toStdString()
                          : std::string();
    }
};

}  // namespace lurker::gui
