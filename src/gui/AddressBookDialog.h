// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Address Book: a tabbed manager for favourite channels, a notify (watch) list,
// and an ignore list. A clean-room functional equivalent of mIRC's address book
// — our own layout. Lists persist to QSettings; the owner wires the behaviours
// (join, ISON polling for notify, message filtering for ignore).
#pragma once

#include <QDialog>

class QListWidget;

namespace lurker::gui {

class AddressBookDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddressBookDialog(QWidget* parent = nullptr);

    // Mark which notify nicks are currently online (● vs ○).
    void setNotifyOnline(const QStringList& onlineNicks);

    static QStringList channels();  // favourites/channels
    static QStringList notifyList();
    static QStringList ignoreList();

signals:
    void joinRequested(const QString& channel);
    void listsChanged();  // notify/ignore edited → owner reloads its sets

private:
    void persist();
    void addTo(QListWidget* list, const QString& title, const QString& prompt, const QString& seed);

    QListWidget* m_channels = nullptr;
    QListWidget* m_notify = nullptr;
    QListWidget* m_ignore = nullptr;
};

}  // namespace lurker::gui
