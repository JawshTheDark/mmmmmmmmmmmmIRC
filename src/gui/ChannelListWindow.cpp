// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ChannelListWindow.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace lurker::gui {

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

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({tr("Channel"), tr("Users"), tr("Topic")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 200);
    m_table->setColumnWidth(1, 70);
    outer->addWidget(m_table, 1);

    m_status = new QLabel(tr("Use /list or the toolbar to fetch channels."), this);
    outer->addWidget(m_status);

    auto join = [this] {
        const int row = m_table->currentRow();
        if (row < 0) return;
        if (auto* it = m_table->item(row, 0)) emit joinRequested(it->text());
    };
    connect(joinBtn, &QPushButton::clicked, this, join);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [join](int, int) { join(); });
    connect(m_filter, &QLineEdit::textChanged, this, &ChannelListWindow::applyFilter);
}

void ChannelListWindow::listBegin() {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_count = 0;
    m_loading = true;
    m_status->setText(tr("Loading channels..."));
}

void ChannelListWindow::listAdd(const QString& channel, int users, const QString& topic) {
    if (!m_loading) listBegin();  // some servers omit 321
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(channel));
    auto* u = new QTableWidgetItem;
    u->setData(Qt::DisplayRole, users);  // numeric sort
    u->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(row, 1, u);
    m_table->setItem(row, 2, new QTableWidgetItem(topic));
    ++m_count;
    if ((m_count % 200) == 0) m_status->setText(tr("Loading channels... %1").arg(m_count));
}

void ChannelListWindow::listEnd() {
    m_loading = false;
    m_table->setSortingEnabled(true);
    m_table->sortItems(1, Qt::DescendingOrder);  // busiest first, like mIRC
    m_status->setText(tr("%1 channels").arg(m_count));
    applyFilter();
}

void ChannelListWindow::applyFilter() {
    const QString needle = m_filter->text().trimmed();
    int shown = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        bool match = needle.isEmpty();
        if (!match) {
            const QString name = m_table->item(r, 0) ? m_table->item(r, 0)->text() : QString();
            const QString topic = m_table->item(r, 2) ? m_table->item(r, 2)->text() : QString();
            match = name.contains(needle, Qt::CaseInsensitive) || topic.contains(needle, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(r, !match);
        if (match) ++shown;
    }
    if (!needle.isEmpty()) m_status->setText(tr("%1 of %2 channels").arg(shown).arg(m_count));
    else if (!m_loading) m_status->setText(tr("%1 channels").arg(m_count));
}

}  // namespace lurker::gui
