// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ServerListDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lurker::gui {

QString ServerEntry::display() const {
    QString s = QStringLiteral("%1:%2%3").arg(host).arg(port).arg(tls ? QStringLiteral(" (TLS)") : QString());
    return name.isEmpty() ? s : QStringLiteral("%1 — %2").arg(name, s);
}

QString ServerEntry::store() const {
    return QStringLiteral("%1\t%2\t%3\t%4").arg(name, host).arg(port).arg(tls ? 1 : 0);
}

ServerEntry ServerEntry::parse(const QString& s) {
    ServerEntry e;
    if (s.contains(QLatin1Char('\t'))) {  // our stored format: name \t host \t port \t ssl
        const QStringList p = s.split(QLatin1Char('\t'));
        e.name = p.value(0);
        e.host = p.value(1);
        e.port = p.value(2).toInt();
        e.tls = p.value(3) == QLatin1String("1");
        return e;
    }
    // An imported entry: "host:port (SSL)" or "host:+port".
    QString t = s;
    const int sp = t.indexOf(QLatin1Char(' '));
    if (sp >= 0) {
        e.tls = t.mid(sp).contains(QLatin1String("SSL"), Qt::CaseInsensitive) ||
                t.mid(sp).contains(QLatin1String("TLS"), Qt::CaseInsensitive);
        t = t.left(sp);
    }
    const int colon = t.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0) {
        e.host = t.left(colon);
        QString port = t.mid(colon + 1);
        if (port.startsWith(QLatin1Char('+'))) {
            e.tls = true;
            port.remove(0, 1);
        }
        e.port = port.toInt();
    } else {
        e.host = t;
    }
    if (e.port == 0) e.port = e.tls ? 6697 : 6667;
    return e;
}

ServerListDialog::ServerListDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Server List"));
    resize(440, 420);
    auto* outer = new QVBoxLayout(this);
    outer->addWidget(new QLabel(tr("Saved servers — double-click or Connect:"), this));
    m_list = new QListWidget(this);
    outer->addWidget(m_list, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add…"), this);
    auto* edit = new QPushButton(tr("Edit…"), this);
    auto* rem = new QPushButton(tr("Remove"), this);
    auto* conn = new QPushButton(tr("Connect"), this);
    row->addWidget(add);
    row->addWidget(edit);
    row->addWidget(rem);
    row->addStretch(1);
    row->addWidget(conn);
    outer->addLayout(row);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    reload();

    connect(add, &QPushButton::clicked, this, [this] { addOrEdit(-1); });
    connect(edit, &QPushButton::clicked, this, [this] { addOrEdit(m_list->currentRow()); });
    connect(rem, &QPushButton::clicked, this, [this] {
        const int r = m_list->currentRow();
        if (r >= 0 && r < m_servers.size()) {
            m_servers.removeAt(r);
            persist();
            reload();
        }
    });
    auto doConnect = [this] {
        const int r = m_list->currentRow();
        if (r >= 0 && r < m_servers.size()) {
            const ServerEntry& e = m_servers[r];
            emit connectRequested(e.host, e.port, e.tls);
            accept();
        }
    };
    connect(conn, &QPushButton::clicked, this, doConnect);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [doConnect](QListWidgetItem*) { doConnect(); });
}

void ServerListDialog::reload() {
    m_servers.clear();
    QSettings s;
    for (const QString& e : s.value(QStringLiteral("servers/list")).toStringList()) m_servers << ServerEntry::parse(e);
    // Merge imported servers that aren't already present (dedupe by host:port).
    auto has = [this](const ServerEntry& e) {
        for (const ServerEntry& x : m_servers)
            if (x.host.compare(e.host, Qt::CaseInsensitive) == 0 && x.port == e.port) return true;
        return false;
    };
    for (const QString& e : s.value(QStringLiteral("servers/imported")).toStringList()) {
        ServerEntry se = ServerEntry::parse(e);
        if (!se.host.isEmpty() && !has(se)) m_servers << se;
    }
    m_list->clear();
    for (const ServerEntry& e : m_servers) m_list->addItem(e.display());
}

void ServerListDialog::persist() {
    QStringList out;
    for (const ServerEntry& e : m_servers) out << e.store();
    QSettings().setValue(QStringLiteral("servers/list"), out);
}

void ServerListDialog::addOrEdit(int row) {
    const bool editing = row >= 0 && row < m_servers.size();
    ServerEntry e = editing ? m_servers[row] : ServerEntry{};

    QDialog d(this);
    d.setWindowTitle(editing ? tr("Edit Server") : tr("Add Server"));
    auto* f = new QFormLayout(&d);
    auto* host = new QLineEdit(e.host, &d);
    auto* port = new QSpinBox(&d);
    port->setRange(1, 65535);
    port->setValue(e.port ? e.port : 6667);
    auto* tls = new QCheckBox(tr("Use TLS/SSL"), &d);
    tls->setChecked(e.tls);
    auto* name = new QLineEdit(e.name, &d);
    name->setPlaceholderText(tr("optional label"));
    f->addRow(tr("Host"), host);
    f->addRow(tr("Port"), port);
    f->addRow(QString(), tls);
    f->addRow(tr("Label"), name);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(tls, &QCheckBox::toggled, &d, [port](bool on) {
        if (on && port->value() == 6667) port->setValue(6697);
        else if (!on && port->value() == 6697) port->setValue(6667);
    });
    if (d.exec() != QDialog::Accepted || host->text().trimmed().isEmpty()) return;

    e.host = host->text().trimmed();
    e.port = port->value();
    e.tls = tls->isChecked();
    e.name = name->text().trimmed();
    if (editing) m_servers[row] = e;
    else m_servers << e;
    persist();
    reload();
}

}  // namespace lurker::gui
