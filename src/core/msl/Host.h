// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The MSL engine reaches the outside world only through this interface, so the
// language core stays Qt-free and fully testable with a fake Host. The GUI
// implements it with Qt (windows, timers, files, the IRC connection); tests
// implement it in-memory.
#pragma once

#include <string>
#include <vector>

#include "core/msl/Dialog.h"

namespace lurker::msl {

// The "active" context a script runs in: which window/target it affects by
// default ($active, $chan, and where /echo and /msg without a target land).
struct ScriptTarget {
    int networkId = -1;
    std::string window;  // channel, query nick, or a custom @window; "" = status
};

class Host {
public:
    virtual ~Host() = default;

    // /echo and friends: print a line to a window ("" or "@status" = status,
    // "-a" active handled by the caller). `window` empty means the active one.
    virtual void echo(const ScriptTarget& where, const std::string& text) = 0;

    // Send to IRC: PRIVMSG (/msg, /say), action (/me/describe), raw line (/raw).
    virtual void sendMessage(int networkId, const std::string& target, const std::string& text) = 0;
    virtual void sendAction(int networkId, const std::string& target, const std::string& text) = 0;
    virtual void sendRaw(int networkId, const std::string& line) = 0;

    // Context the script was triggered in.
    virtual ScriptTarget activeTarget() const = 0;
    virtual std::string me(int networkId) const = 0;  // $me

    // IRC state queries for identifiers like $nick(#,N), $chan(N), $comchan,
    // $ison. Defaults are empty so non-GUI/test hosts need not implement them;
    // the GUI answers from its live member/buffer state.
    //   nicks on a channel, in nicklist order (sigils stripped) — $nick(#,N)
    virtual std::vector<std::string> channelNicks(int networkId, const std::string& channel) const {
        (void)networkId;
        (void)channel;
        return {};
    }
    //   channels we are currently in on this network — $chan(N)
    virtual std::vector<std::string> myChannels(int networkId) const {
        (void)networkId;
        return {};
    }
    //   is `nick` present on `channel`? — used by $comchan / $ison
    virtual bool isOn(int networkId, const std::string& channel, const std::string& nick) const {
        (void)networkId;
        (void)channel;
        (void)nick;
        return false;
    }
    //   network display name ($network) and server host ($server) for a network
    virtual std::string networkName(int networkId) const {
        (void)networkId;
        return {};
    }
    virtual std::string serverHost(int networkId) const {
        (void)networkId;
        return {};
    }
    //   loaded script files ($script(N), $script(0)=count)
    virtual std::vector<std::string> scriptFiles() const { return {}; }
    //   open window names ($window(N), $window(0)=count, $window(name) exists)
    virtual std::vector<std::string> windowNames() const { return {}; }
    //   a window's line count and the text of its Nth line ($line)
    virtual int windowLineCount(const std::string& window) const {
        (void)window;
        return 0;
    }
    virtual std::string windowLine(const std::string& window, int n) const {
        (void)window;
        (void)n;
        return {};
    }

    // Wall-clock, overridable in tests ($ctime, $time, timers).
    virtual long long unixTime() const = 0;

    // Directories for $mircdir (app/config dir) and $scriptdir (the running
    // script's folder). Both include a trailing separator when non-empty.
    virtual std::string appDir() const { return {}; }
    virtual std::string scriptDir() const { return {}; }

    // Timers: /timer. `name` empty picks an auto name; returns the name used.
    virtual std::string startTimer(const std::string& name, int repeat, double intervalSeconds,
                                   const std::string& command) = 0;
    virtual void stopTimer(const std::string& name) = 0;

    // A line the engine wants surfaced to the user as an error (* /cmd: ...).
    virtual void scriptError(const std::string& text) = 0;

    // The user access list changed via /auser or /ruser; persist it (the GUI
    // writes the serialized list back to the Users section). Default: no-op.
    virtual void usersChanged(const std::string& serialized) { (void)serialized; }

    // Modal input prompt for $? / $$? / $input. Returns the entered text (empty
    // if cancelled). `masked` hides the input (passwords). Default: empty.
    virtual std::string inputDialog(const std::string& prompt, bool masked) {
        (void)prompt;
        (void)masked;
        return {};
    }
    // Directory / file chooser dialogs for $sdir / $sfile. Default: empty.
    virtual std::string chooseDir(const std::string& start, const std::string& title) {
        (void)start;
        (void)title;
        return {};
    }
    virtual std::string chooseFile(const std::string& start, const std::string& title, const std::string& filter) {
        (void)start;
        (void)title;
        (void)filter;
        return {};
    }

    // Dialogs (/dialog, /did, $did). Default no-ops so non-GUI hosts ignore them;
    // the GUI renders a real dialog from the table and reports control events.
    virtual void openDialog(const std::string& instance, const DialogTable& table, bool modal) {
        (void)instance;
        (void)table;
        (void)modal;
    }
    virtual void closeDialog(const std::string& instance) { (void)instance; }
    virtual void didCommand(const std::string& instance, int id, const std::string& switches,
                            const std::string& text) {
        (void)instance;
        (void)id;
        (void)switches;
        (void)text;
    }
    virtual std::string didQuery(const std::string& instance, int id, const std::string& prop) const {
        (void)instance;
        (void)id;
        (void)prop;
        return {};
    }
    // /dll and $dll: call a DLL export. We don't load native DLLs; the GUI host
    // emulates known addons (mdx.dll) so their scripts run. Default: no-op.
    virtual std::string dllCall(const std::string& file, const std::string& proc, const std::string& data) {
        (void)file;
        (void)proc;
        (void)data;
        return {};
    }

    // Filesystem (/write, $read, INI, $exists, ...). Defaults do nothing so a
    // non-GUI host has no disk access; the GUI maps these to QFile/QDir.
    virtual std::string fileRead(const std::string& path) const {
        (void)path;
        return {};
    }
    virtual bool fileWrite(const std::string& path, const std::string& content) {
        (void)path;
        (void)content;
        return false;
    }
    virtual bool fileExists(const std::string& path) const {
        (void)path;
        return false;
    }
    virtual bool dirExists(const std::string& path) const {
        (void)path;
        return false;
    }
    virtual long long fileSize(const std::string& path) const {
        (void)path;
        return -1;
    }
    virtual bool makeDir(const std::string& path) {
        (void)path;
        return false;
    }
    virtual bool removePath(const std::string& path) {
        (void)path;
        return false;
    }
    virtual std::vector<std::string> listDir(const std::string& dir, const std::string& wildcard) const {
        (void)dir;
        (void)wildcard;
        return {};
    }

    // Toggle a main-window UI bar (/switchbar, /toolbar, /treebar, /menubar,
    // /nicklist, /titlebar). mode: 0 hide, 1 show, 2 toggle. Default no-op.
    virtual void setBarVisible(const std::string& bar, int mode) {
        (void)bar;
        (void)mode;
    }

    // Custom windows (/window @name, /clear). Default no-ops.
    virtual void openWindow(const std::string& name, const std::string& switches) {
        (void)name;
        (void)switches;
    }
    virtual void clearWindow(const std::string& name) { (void)name; }

    // Picture-window drawing (/drawrect, /drawtext, ...). op is rect/text/line/
    // dot/fill/clear; args are the command params after the window name.
    virtual void drawOp(const std::string& window, const std::string& op, const std::vector<std::string>& args) {
        (void)window;
        (void)op;
        (void)args;
    }

    // TCP sockets (/sockopen, /sockwrite, $sock, on SOCKOPEN/READ/CLOSE). Defaults
    // do nothing; the GUI backs them with QTcpSocket/QSslSocket.
    virtual void sockOpen(const std::string& name, const std::string& host, int port, bool ssl) {
        (void)name;
        (void)host;
        (void)port;
        (void)ssl;
    }
    virtual void sockWrite(const std::string& name, const std::string& data, bool newline) {
        (void)name;
        (void)data;
        (void)newline;
    }
    virtual void sockClose(const std::string& name) { (void)name; }
    // Reads one line from the socket's buffer; ok=false when none is ready.
    virtual std::string sockReadLine(const std::string& name, bool& ok) {
        (void)name;
        ok = false;
        return {};
    }
    virtual std::string sockInfo(const std::string& name, const std::string& prop) const {
        (void)name;
        (void)prop;
        return {};
    }
};

}  // namespace lurker::msl
