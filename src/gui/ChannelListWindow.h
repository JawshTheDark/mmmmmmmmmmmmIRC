// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The Channels List window (/list): a sortable table of channel / users / topic
// filled from RPL_LIST (322) replies, with a filter box. Double-click (or the
// Join button) joins a channel. mIRC shows /list here, not in the status window.
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTableWidget;

namespace lurker::gui {

class ChannelListWindow : public QWidget {
    Q_OBJECT
public:
    explicit ChannelListWindow(QWidget* parent = nullptr);

    void listBegin();                                                   // 321: clear + mark loading
    void listAdd(const QString& channel, int users, const QString& topic);  // 322
    void listEnd();                                                     // 323: final count

signals:
    void joinRequested(const QString& channel);

private:
    void applyFilter();

    QLineEdit* m_filter = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    int m_count = 0;
    bool m_loading = false;
};

}  // namespace lurker::gui
