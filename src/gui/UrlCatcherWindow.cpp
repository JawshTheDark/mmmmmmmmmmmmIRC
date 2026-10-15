// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/UrlCatcherWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace lurker::gui {

UrlCatcherWindow::UrlCatcherWindow(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout;
    auto* open = new QPushButton(tr("Open"), this);
    auto* copy = new QPushButton(tr("Copy"), this);
    auto* clear = new QPushButton(tr("Clear"), this);
    bar->addWidget(open);
    bar->addWidget(copy);
    bar->addWidget(clear);
    bar->addStretch(1);
    outer->addLayout(bar);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({tr("Time"), tr("From"), tr("Window"), tr("URL")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 64);
    m_table->setColumnWidth(1, 110);
    m_table->setColumnWidth(2, 120);
    outer->addWidget(m_table, 1);

    m_status = new QLabel(tr("No links yet."), this);
    outer->addWidget(m_status);

    connect(open, &QPushButton::clicked, this, &UrlCatcherWindow::openSelected);
    connect(copy, &QPushButton::clicked, this, &UrlCatcherWindow::copySelected);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_table->setRowCount(0);
        m_status->setText(tr("No links yet."));
        emit cleared();
    });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { openSelected(); });
}

QString UrlCatcherWindow::selectedUrl() const {
    const int row = m_table->currentRow();
    if (row < 0) return {};
    QTableWidgetItem* it = m_table->item(row, 3);
    return it ? it->text() : QString();
}

void UrlCatcherWindow::openSelected() {
    const QString u = selectedUrl();
    if (!u.isEmpty()) QDesktopServices::openUrl(QUrl(u));
}

void UrlCatcherWindow::copySelected() {
    const QString u = selectedUrl();
    if (!u.isEmpty()) QApplication::clipboard()->setText(u);
}

void UrlCatcherWindow::insertRow(int row, const CaughtUrl& u) {
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(u.time));
    m_table->setItem(row, 1, new QTableWidgetItem(u.from));
    m_table->setItem(row, 2, new QTableWidgetItem(u.target));
    m_table->setItem(row, 3, new QTableWidgetItem(u.url));
}

void UrlCatcherWindow::setUrls(const QList<CaughtUrl>& urls) {
    m_table->setRowCount(0);
    for (const CaughtUrl& u : urls) insertRow(m_table->rowCount(), u);  // list is newest-first already
    m_status->setText(urls.isEmpty() ? tr("No links yet.") : tr("%1 link(s)").arg(urls.size()));
}

void UrlCatcherWindow::addUrl(const CaughtUrl& u) {
    insertRow(0, u);  // newest on top
    m_status->setText(tr("%1 link(s)").arg(m_table->rowCount()));
}

}  // namespace lurker::gui
