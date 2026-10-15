// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ChannelListWindow.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

namespace lurker::gui {

namespace {
// Filters on channel name OR topic, and sorts the Users column numerically.
// Rows stream in live, so the model/view keeps the sort current as /list replies
// arrive — far cheaper than re-sorting a QTableWidget on every insert.
class ChannelProxy : public QSortFilterProxyModel {
public:
    explicit ChannelProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setNeedle(const QString& n) {
        m_needle = n.trimmed();
        invalidate();  // re-filter (version-safe across Qt 6.8+; not the deprecated invalidateFilter)
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (m_needle.isEmpty()) return true;
        const QString name = sourceModel()->index(row, 0, parent).data().toString();
        const QString topic = sourceModel()->index(row, 2, parent).data().toString();
        return name.contains(m_needle, Qt::CaseInsensitive) || topic.contains(m_needle, Qt::CaseInsensitive);
    }
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override {
        if (a.column() == 1)  // Users: numeric
            return a.data(Qt::EditRole).toInt() < b.data(Qt::EditRole).toInt();
        return QSortFilterProxyModel::lessThan(a, b);
    }

private:
    QString m_needle;
};
}  // namespace

ChannelListWindow::ChannelListWindow(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Filter:"), this));
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("name or topic contains..."));
    top->addWidget(m_filter, 1);
    auto* joinBtn = new QPushButton(tr("Join"), this);
    top->addWidget(joinBtn);
    outer->addLayout(top);

    m_model = new QStandardItemModel(0, 3, this);
    m_model->setHorizontalHeaderLabels({tr("Channel"), tr("Users"), tr("Topic")});
    auto* proxy = new ChannelProxy(this);
    proxy->setSourceModel(m_model);
    proxy->setDynamicSortFilter(true);  // re-sort/re-filter as rows arrive
    m_proxy = proxy;

    m_view = new QTableView(this);
    m_view->setModel(m_proxy);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(1, Qt::DescendingOrder);  // busiest first, like mIRC
    m_view->verticalHeader()->setVisible(false);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setColumnWidth(0, 200);
    m_view->setColumnWidth(1, 70);
    outer->addWidget(m_view, 1);

    m_status = new QLabel(tr("Use /list or the toolbar to fetch channels."), this);
    outer->addWidget(m_status);

    connect(joinBtn, &QPushButton::clicked, this, &ChannelListWindow::joinCurrent);
    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex&) { joinCurrent(); });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& t) {
        static_cast<ChannelProxy*>(m_proxy)->setNeedle(t);
        updateStatus();
    });
}

void ChannelListWindow::joinCurrent() {
    const QModelIndex idx = m_view->currentIndex();
    if (!idx.isValid()) return;
    const QString chan = m_proxy->index(idx.row(), 0).data().toString();
    if (!chan.isEmpty()) emit joinRequested(chan);
}

void ChannelListWindow::listBegin() {
    m_model->removeRows(0, m_model->rowCount());
    m_loading = true;
    m_status->setText(tr("Loading channels..."));
}

void ChannelListWindow::listAdd(const QString& channel, int users, const QString& topic) {
    if (!m_loading) listBegin();  // some servers omit RPL_LISTSTART (321)
    auto* name = new QStandardItem(channel);
    auto* u = new QStandardItem;
    u->setData(users, Qt::EditRole);  // int → numeric sort + numeric display
    u->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* t = new QStandardItem(topic);
    m_model->appendRow({name, u, t});
    if ((m_model->rowCount() % 200) == 0) updateStatus();
}

void ChannelListWindow::listEnd() {
    m_loading = false;
    updateStatus();
}

void ChannelListWindow::updateStatus() {
    const int total = m_model->rowCount();
    const int shown = m_proxy->rowCount();
    if (!m_filter->text().trimmed().isEmpty()) {
        m_status->setText(tr("%1 of %2 channels").arg(shown).arg(total));
    } else {
        m_status->setText(m_loading ? tr("Loading channels... %1").arg(total) : tr("%1 channels").arg(total));
    }
}

}  // namespace lurker::gui
