// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The Channels List window (/list): a sortable Channel / Users / Topic table
// filled live from RPL_LIST (322) replies, with a filter box. It uses a
// model/view with a sort-filter proxy so rows appear and re-sort as they stream
// in (fast for thousands of channels). Double-click / Join joins a channel.
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QTableView;
class QStandardItemModel;
class QSortFilterProxyModel;

namespace lurker::gui {

class ChannelListWindow : public QWidget {
    Q_OBJECT
public:
    explicit ChannelListWindow(QWidget* parent = nullptr);

    void listBegin();                                                       // 321: clear + mark loading
    void listAdd(const QString& channel, int users, const QString& topic);  // 322 (live)
    void listEnd();                                                         // 323: final count

signals:
    void joinRequested(const QString& channel);

private:
    void joinCurrent();
    void updateStatus();

    QLineEdit* m_filter = nullptr;
    QTableView* m_view = nullptr;
    QStandardItemModel* m_model = nullptr;
    QSortFilterProxyModel* m_proxy = nullptr;
    QLabel* m_status = nullptr;
    bool m_loading = false;
};

}  // namespace lurker::gui
