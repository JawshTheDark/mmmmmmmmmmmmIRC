// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/AddressBookDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QVBoxLayout>

namespace lurker::gui {

namespace {
const char* kChannelsKey = "favourites/channels";
const char* kNotifyKey = "notify/list";
const char* kIgnoreKey = "ignore/list";

QListWidget* fill(QListWidget* w, const char* key) {
    w->addItems(QSettings().value(QString::fromLatin1(key)).toStringList());
    return w;
}
}  // namespace

QStringList AddressBookDialog::channels() { return QSettings().value(QLatin1String(kChannelsKey)).toStringList(); }
QStringList AddressBookDialog::notifyList() { return QSettings().value(QLatin1String(kNotifyKey)).toStringList(); }
QStringList AddressBookDialog::ignoreList() { return QSettings().value(QLatin1String(kIgnoreKey)).toStringList(); }

AddressBookDialog::AddressBookDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Address Book"));
    resize(380, 440);
    auto* outer = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs, 1);

    // Build one list tab with Add/Remove (+ an optional extra button).
    auto makeTab = [&](QListWidget*& list, const QString& hint, const QString& addTitle, const QString& addPrompt,
                       const QString& seed, QPushButton* extra = nullptr) {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        v->addWidget(new QLabel(hint, page));
        list = new QListWidget(page);
        v->addWidget(list, 1);
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton(tr("Add…"), page);
        auto* rem = new QPushButton(tr("Remove"), page);
        row->addWidget(add);
        row->addWidget(rem);
        if (extra) {
            extra->setParent(page);
            row->addWidget(extra);
        }
        row->addStretch(1);
        v->addLayout(row);
        connect(add, &QPushButton::clicked, this, [this, list, addTitle, addPrompt, seed] {
            addTo(list, addTitle, addPrompt, seed);
        });
        connect(rem, &QPushButton::clicked, this, [this, list] {
            qDeleteAll(list->selectedItems());
            persist();
            emit listsChanged();
        });
        return page;
    };

    auto* joinBtn = new QPushButton(tr("Join"));
    tabs->addTab(makeTab(m_channels, tr("Favourite channels — double-click to join:"), tr("Add channel"),
                         tr("Channel:"), QStringLiteral("#"), joinBtn),
                 tr("Channels"));
    tabs->addTab(makeTab(m_notify, tr("Notify list — nicks you're told about when online:"), tr("Add notify"),
                         tr("Nick:"), QString()),
                 tr("Notify"));
    tabs->addTab(makeTab(m_ignore, tr("Ignore list — masks whose messages are hidden:"), tr("Add ignore"),
                         tr("Mask (nick or nick!user@host):"), QStringLiteral("*!*@")),
                 tr("Ignore"));

    fill(m_channels, kChannelsKey);
    fill(m_notify, kNotifyKey);
    fill(m_ignore, kIgnoreKey);

    auto joinCurrent = [this] {
        if (QListWidgetItem* it = m_channels->currentItem()) emit joinRequested(it->text());
    };
    connect(joinBtn, &QPushButton::clicked, this, joinCurrent);
    connect(m_channels, &QListWidget::itemDoubleClicked, this, [joinCurrent](QListWidgetItem*) { joinCurrent(); });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
}

void AddressBookDialog::addTo(QListWidget* list, const QString& title, const QString& prompt, const QString& seed) {
    bool ok = false;
    const QString v = QInputDialog::getText(this, title, prompt, QLineEdit::Normal, seed, &ok).trimmed();
    if (!ok || v.isEmpty() || v == seed) return;
    if (list->findItems(v, Qt::MatchFixedString).isEmpty()) list->addItem(v);
    persist();
    emit listsChanged();
}

void AddressBookDialog::persist() {
    auto items = [](QListWidget* w) {
        QStringList out;
        for (int i = 0; i < w->count(); ++i) {
            QString t = w->item(i)->text();
            if (t.startsWith(QLatin1String("● ")) || t.startsWith(QLatin1String("○ "))) t = t.mid(2);
            out << t;
        }
        return out;
    };
    QSettings s;
    s.setValue(QLatin1String(kChannelsKey), items(m_channels));
    s.setValue(QLatin1String(kNotifyKey), items(m_notify));
    s.setValue(QLatin1String(kIgnoreKey), items(m_ignore));
}

void AddressBookDialog::setNotifyOnline(const QStringList& onlineNicks) {
    for (int i = 0; i < m_notify->count(); ++i) {
        QListWidgetItem* it = m_notify->item(i);
        QString nick = it->text();
        if (nick.startsWith(QLatin1String("● ")) || nick.startsWith(QLatin1String("○ "))) nick = nick.mid(2);
        const bool online = onlineNicks.contains(nick, Qt::CaseInsensitive);
        it->setText((online ? QStringLiteral("● ") : QStringLiteral("○ ")) + nick);
    }
}

}  // namespace lurker::gui
