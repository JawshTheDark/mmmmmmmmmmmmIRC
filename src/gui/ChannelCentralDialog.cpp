// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ChannelCentralDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lurker::gui {

namespace {
// Standard IRC channel modes (RFC 2811 / modern.ircdocs.horse) with plain
// descriptions. Letter → human label; the toggles below use these.
struct ModeDef {
    char letter;
    const char* label;
};
const ModeDef kModes[] = {
    {'n', "No messages from outside the channel (+n)"},
    {'t', "Only operators can change the topic (+t)"},
    {'m', "Moderated — only voiced users and ops can talk (+m)"},
    {'i', "Invite only (+i)"},
    {'s', "Secret (+s)"},
    {'p', "Private (+p)"},
};
}  // namespace

ChannelCentralDialog::ChannelCentralDialog(int net, const QString& channel, QWidget* parent)
    : QDialog(parent), m_channel(channel), m_net(net) {
    setWindowTitle(tr("Channel Central — %1").arg(channel));
    resize(420, 520);
    auto* outer = new QVBoxLayout(this);

    // Topic.
    auto* topicBox = new QGroupBox(tr("Topic"), this);
    auto* tl = new QHBoxLayout(topicBox);
    m_topic = new QLineEdit(topicBox);
    auto* setTopic = new QPushButton(tr("Set"), topicBox);
    tl->addWidget(m_topic, 1);
    tl->addWidget(setTopic);
    outer->addWidget(topicBox);

    // Modes.
    auto* modeBox = new QGroupBox(tr("Modes"), this);
    auto* ml = new QVBoxLayout(modeBox);
    for (const ModeDef& m : kModes) {
        auto* cb = new QCheckBox(tr(m.label), modeBox);
        m_modeChecks.insert(QChar(m.letter), cb);
        ml->addWidget(cb);
    }
    auto* keyRow = new QHBoxLayout;
    m_keyOn = new QCheckBox(tr("Key (+k)"), modeBox);
    m_key = new QLineEdit(modeBox);
    m_key->setPlaceholderText(tr("channel password"));
    keyRow->addWidget(m_keyOn);
    keyRow->addWidget(m_key, 1);
    ml->addLayout(keyRow);
    auto* limitRow = new QHBoxLayout;
    m_limitOn = new QCheckBox(tr("User limit (+l)"), modeBox);
    m_limit = new QSpinBox(modeBox);
    m_limit->setRange(1, 99999);
    m_limit->setValue(50);
    limitRow->addWidget(m_limitOn);
    limitRow->addWidget(m_limit);
    limitRow->addStretch(1);
    ml->addLayout(limitRow);
    auto* applyModes = new QPushButton(tr("Apply Modes"), modeBox);
    ml->addWidget(applyModes, 0, Qt::AlignRight);
    outer->addWidget(modeBox);

    // Ban list.
    auto* banBox = new QGroupBox(tr("Ban list (+b)"), this);
    auto* bl = new QVBoxLayout(banBox);
    m_bans = new QListWidget(banBox);
    bl->addWidget(m_bans, 1);
    auto* banBtns = new QHBoxLayout;
    auto* addBan = new QPushButton(tr("Add…"), banBox);
    auto* delBan = new QPushButton(tr("Remove"), banBox);
    banBtns->addWidget(addBan);
    banBtns->addWidget(delBan);
    banBtns->addStretch(1);
    bl->addLayout(banBtns);
    outer->addWidget(banBox, 1);

    m_status = new QLabel(this);
    outer->addWidget(m_status);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);

    connect(setTopic, &QPushButton::clicked, this,
            [this] { emit topicChangeRequested(m_net, m_channel, m_topic->text()); });
    connect(applyModes, &QPushButton::clicked, this, &ChannelCentralDialog::applyModes);
    connect(addBan, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString mask = QInputDialog::getText(this, tr("Add ban"), tr("Ban mask (e.g. *!*@host):"),
                                                   QLineEdit::Normal, QStringLiteral("*!*@"), &ok);
        if (ok && !mask.trimmed().isEmpty())
            emit modeChangeRequested(m_net, m_channel, QStringLiteral("+b %1").arg(mask.trimmed()));
    });
    connect(delBan, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* it = m_bans->currentItem())
            emit modeChangeRequested(m_net, m_channel, QStringLiteral("-b %1").arg(it->text()));
    });

    emit stateRefreshRequested(m_net, m_channel);  // ask the owner to query current modes + bans
}

void ChannelCentralDialog::setTopicText(const QString& topic) { m_topic->setText(topic); }

void ChannelCentralDialog::setCurrentModes(const QString& modes, const QString& key, int limit) {
    m_curModes = modes;
    m_curKey = key;
    m_curLimit = limit;
    for (auto it = m_modeChecks.constBegin(); it != m_modeChecks.constEnd(); ++it) {
        QSignalBlocker b(it.value());
        it.value()->setChecked(modes.contains(it.key()));
    }
    QSignalBlocker bk(m_keyOn), bl(m_limitOn);
    m_keyOn->setChecked(!key.isEmpty());
    if (!key.isEmpty()) m_key->setText(key);
    m_limitOn->setChecked(limit > 0);
    if (limit > 0) m_limit->setValue(limit);
}

void ChannelCentralDialog::setBanList(const QStringList& masks) {
    m_bans->clear();
    m_bans->addItems(masks);
}

void ChannelCentralDialog::addBanMask(const QString& mask) {
    if (m_bans->findItems(mask, Qt::MatchExactly).isEmpty()) m_bans->addItem(mask);
}

void ChannelCentralDialog::applyModes() {
    // Build the minimal +/- delta from current server state to the chosen toggles.
    QString plus, minus;
    QStringList params;
    for (auto it = m_modeChecks.constBegin(); it != m_modeChecks.constEnd(); ++it) {
        const bool want = it.value()->isChecked();
        const bool have = m_curModes.contains(it.key());
        if (want && !have) plus += it.key();
        else if (!want && have) minus += it.key();
    }
    // Key.
    const bool wantKey = m_keyOn->isChecked() && !m_key->text().trimmed().isEmpty();
    if (wantKey && (m_curKey.isEmpty() || m_curKey != m_key->text().trimmed())) {
        if (!m_curKey.isEmpty()) {  // change: drop the old one first
            minus += 'k';
            params.prepend(m_curKey);
        }
        plus += 'k';
        params << m_key->text().trimmed();
    } else if (!wantKey && !m_curKey.isEmpty()) {
        minus += 'k';
        params.prepend(m_curKey);
    }
    // Limit.
    const bool wantLimit = m_limitOn->isChecked();
    if (wantLimit && m_limit->value() != m_curLimit) {
        plus += 'l';
        params << QString::number(m_limit->value());
    } else if (!wantLimit && m_curLimit > 0) {
        minus += 'l';
    }
    QString spec;
    if (!plus.isEmpty()) spec += '+' + plus;
    if (!minus.isEmpty()) spec += '-' + minus;
    if (spec.isEmpty()) {
        m_status->setText(tr("No mode changes."));
        return;
    }
    if (!params.isEmpty()) spec += ' ' + params.join(' ');
    emit modeChangeRequested(m_net, m_channel, spec);
    m_status->setText(tr("Sent: MODE %1 %2").arg(m_channel, spec));
}

}  // namespace lurker::gui
