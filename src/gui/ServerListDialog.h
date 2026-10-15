// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Server List: a saved list of IRC servers (host / port / TLS / label) to add,
// edit, remove and connect to — including the servers brought in by the mIRC
// import. A clean-room functional equivalent of mIRC's server list, our own
// design. "Connect" prefills the Connect dialog for the chosen server.
#pragma once

#include <QDialog>

class QListWidget;

namespace lurker::gui {

struct ServerEntry {
    QString name;
    QString host;
    int port = 6667;
    bool tls = false;
    QString display() const;
    QString store() const;                       // tab-joined, for QSettings
    static ServerEntry parse(const QString& s);  // from store() or an imported "host:port (SSL)"
};

class ServerListDialog : public QDialog {
    Q_OBJECT
public:
    explicit ServerListDialog(QWidget* parent = nullptr);

signals:
    void connectRequested(const QString& host, int port, bool tls);

private:
    void reload();
    void persist();
    void addOrEdit(int row);  // row < 0 → add

    QListWidget* m_list = nullptr;
    QList<ServerEntry> m_servers;
};

}  // namespace lurker::gui
