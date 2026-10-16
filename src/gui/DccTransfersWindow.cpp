// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/DccTransfersWindow.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace lurker::gui {

using lurker::backend::DccTransfer;

namespace {
QString humanBytes(qlonglong n) {
    if (n <= 0) return QStringLiteral("—");
    static const char* u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(n);
    int i = 0;
    while (v >= 1024.0 && i < 4) {
        v /= 1024.0;
        ++i;
    }
    return QStringLiteral("%1 %2").arg(v, 0, 'f', i == 0 ? 0 : 1).arg(QString::fromLatin1(u[i]));
}

QString stateLabel(const DccTransfer& t) {
    if (t.state == QLatin1String("pending_approval") || t.state == QLatin1String("requested"))
        return QObject::tr("Offered");
    if (t.state == QLatin1String("connecting")) return QObject::tr("Connecting");
    if (t.state == QLatin1String("receiving")) return QObject::tr("Receiving");
    if (t.state == QLatin1String("stalled")) return QObject::tr("Stalled");
    if (t.state == QLatin1String("verifying")) return QObject::tr("Verifying");
    if (t.state == QLatin1String("completed")) return t.fileDeleted ? QObject::tr("Completed (removed)") : QObject::tr("Completed");
    if (t.state == QLatin1String("failed")) return t.error.isEmpty() ? QObject::tr("Failed") : QObject::tr("Failed: %1").arg(t.error);
    if (t.state == QLatin1String("rejected")) return QObject::tr("Rejected");
    if (t.state == QLatin1String("cancelled")) return QObject::tr("Cancelled");
    return t.state;
}
}  // namespace

DccTransfersWindow::DccTransfersWindow(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout;
    m_send = new QPushButton(tr("Send File…"), this);
    m_accept = new QPushButton(tr("Accept"), this);
    m_reject = new QPushButton(tr("Reject"), this);
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_save = new QPushButton(tr("Save As…"), this);
    m_delete = new QPushButton(tr("Delete"), this);
    bar->addWidget(m_send);
    bar->addSpacing(12);
    for (QPushButton* b : {m_accept, m_reject, m_cancel, m_save, m_delete}) bar->addWidget(b);
    connect(m_send, &QPushButton::clicked, this, [this] { emit sendRequested(); });
    bar->addStretch(1);
    outer->addLayout(bar);

    m_table = new QTableWidget(0, 6, this);
    m_table->setHorizontalHeaderLabels(
        {tr("Dir"), tr("Peer"), tr("File"), tr("Size"), tr("Progress"), tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 44);
    m_table->setColumnWidth(1, 110);
    m_table->setColumnWidth(2, 220);
    m_table->setColumnWidth(3, 80);
    m_table->setColumnWidth(4, 120);
    outer->addWidget(m_table, 1);

    m_status = new QLabel(tr("No transfers."), this);
    outer->addWidget(m_status);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DccTransfersWindow::updateButtons);
    connect(m_accept, &QPushButton::clicked, this, [this] {
        if (int id = selectedId()) emit acceptRequested(id);
    });
    connect(m_reject, &QPushButton::clicked, this, [this] {
        if (int id = selectedId()) emit rejectRequested(id);
    });
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        if (int id = selectedId()) emit cancelRequested(id);
    });
    connect(m_save, &QPushButton::clicked, this, [this] {
        const int id = selectedId();
        if (id && m_byId.contains(id)) emit saveRequested(id, m_byId.value(id).filename);
    });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        if (int id = selectedId()) emit deleteRequested(id);
    });
    updateButtons();
}

int DccTransfersWindow::selectedId() const {
    const int row = m_table->currentRow();
    if (row < 0) return 0;
    QTableWidgetItem* it = m_table->item(row, 0);
    return it ? it->data(Qt::UserRole).toInt() : 0;
}

void DccTransfersWindow::updateButtons() {
    const int id = selectedId();
    const bool have = id && m_byId.contains(id);
    const DccTransfer t = have ? m_byId.value(id) : DccTransfer{};
    const bool incoming = t.direction != QLatin1String("outgoing");
    m_accept->setEnabled(have && incoming && t.awaitingApproval());
    m_reject->setEnabled(have && incoming && t.awaitingApproval());
    m_cancel->setEnabled(have && t.active() && !t.awaitingApproval());
    m_save->setEnabled(have && t.completed() && !t.fileDeleted);
    m_delete->setEnabled(have && t.completed() && !t.fileDeleted);
}

void DccTransfersWindow::writeRow(int row, const DccTransfer& t) {
    auto* dir = new QTableWidgetItem(t.direction == QLatin1String("outgoing") ? QStringLiteral("↑") : QStringLiteral("↓"));
    dir->setData(Qt::UserRole, t.id);
    dir->setTextAlignment(Qt::AlignCenter);
    m_table->setItem(row, 0, dir);
    m_table->setItem(row, 1, new QTableWidgetItem(t.peer));
    m_table->setItem(row, 2, new QTableWidgetItem(t.filename));
    m_table->setItem(row, 3, new QTableWidgetItem(humanBytes(t.size)));

    auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, 4));
    if (!bar) {
        bar = new QProgressBar;
        bar->setTextVisible(true);
        bar->setRange(0, 100);
        m_table->setCellWidget(row, 4, bar);
    }
    const int pct = t.size > 0 ? static_cast<int>(qMin<qlonglong>(100, t.received * 100 / t.size))
                               : (t.completed() ? 100 : 0);
    bar->setValue(t.completed() ? 100 : pct);

    m_table->setItem(row, 5, new QTableWidgetItem(stateLabel(t)));
}

void DccTransfersWindow::upsert(const DccTransfer& t) {
    m_byId.insert(t.id, t);
    int row = m_rowById.value(t.id, -1);
    if (row < 0) {
        row = m_table->rowCount();
        m_table->insertRow(row);
        m_rowById.insert(t.id, row);
    }
    writeRow(row, t);
    m_status->setText(tr("%1 transfer(s)").arg(m_table->rowCount()));
    updateButtons();
}

void DccTransfersWindow::setTransfers(const QList<DccTransfer>& transfers) {
    m_table->setRowCount(0);
    m_rowById.clear();
    m_byId.clear();
    for (const DccTransfer& t : transfers) upsert(t);
    if (transfers.isEmpty()) m_status->setText(tr("No transfers."));
    else m_table->selectRow(0);
}

}  // namespace lurker::gui
