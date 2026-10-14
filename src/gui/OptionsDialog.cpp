// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/OptionsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "gui/Theme.h"

namespace lurker::gui {

OptionsDialog::OptionsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Options"));
    resize(460, 380);
    auto* outer = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs, 1);

    // Connect / identity.
    auto* conn = new QWidget;
    auto* cf = new QFormLayout(conn);
    m_nick = new QLineEdit(conn);
    m_altNick = new QLineEdit(conn);
    m_user = new QLineEdit(conn);
    m_realname = new QLineEdit(conn);
    cf->addRow(tr("Nickname"), m_nick);
    cf->addRow(tr("Alternative"), m_altNick);
    cf->addRow(tr("Username"), m_user);
    cf->addRow(tr("Real name"), m_realname);
    tabs->addTab(conn, tr("Connect"));

    // Display.
    auto* disp = new QWidget;
    auto* df = new QFormLayout(disp);
    m_theme = new QComboBox(disp);
    for (const Theme& t : availableThemes()) m_theme->addItem(t.name, t.id);
    m_timestamps = new QCheckBox(tr("Show timestamps"), disp);
    m_timeFormat = new QComboBox(disp);
    m_timeFormat->addItems({QStringLiteral("HH:mm"), QStringLiteral("HH:mm:ss"), QStringLiteral("hh:mm ap")});
    m_scrollback = new QSpinBox(disp);
    m_scrollback->setRange(100, 100000);
    m_scrollback->setSingleStep(500);
    m_stripColors = new QCheckBox(tr("Strip mIRC colors from messages"), disp);
    df->addRow(tr("Theme"), m_theme);
    df->addRow(QString(), m_timestamps);
    df->addRow(tr("Time format"), m_timeFormat);
    df->addRow(tr("Scrollback lines"), m_scrollback);
    df->addRow(QString(), m_stripColors);
    tabs->addTab(disp, tr("Display"));

    // IRC.
    auto* irc = new QWidget;
    auto* irf = new QFormLayout(irc);
    m_logging = new QCheckBox(tr("Log channels and queries"), irc);
    m_logDir = new QLineEdit(irc);
    irf->addRow(QString(), m_logging);
    irf->addRow(tr("Log folder"), m_logDir);
    tabs->addTab(irc, tr("IRC"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    load();
}

void OptionsDialog::load() {
    QSettings s;
    m_nick->setText(s.value(QStringLiteral("id/nick")).toString());
    m_altNick->setText(s.value(QStringLiteral("id/altNick")).toString());
    m_user->setText(s.value(QStringLiteral("id/user")).toString());
    m_realname->setText(s.value(QStringLiteral("id/realname"), QStringLiteral("Lurker Desktop")).toString());
    const QString theme = s.value(QStringLiteral("theme"), QStringLiteral("default")).toString();
    m_theme->setCurrentIndex(qMax(0, m_theme->findData(theme)));
    m_timestamps->setChecked(s.value(QStringLiteral("display/timestamps"), true).toBool());
    m_timeFormat->setCurrentText(s.value(QStringLiteral("display/timeFormat"), QStringLiteral("HH:mm")).toString());
    m_scrollback->setValue(s.value(QStringLiteral("display/scrollback"), 5000).toInt());
    m_stripColors->setChecked(s.value(QStringLiteral("display/stripColors"), false).toBool());
    m_logging->setChecked(s.value(QStringLiteral("irc/logging"), false).toBool());
    m_logDir->setText(s.value(QStringLiteral("irc/logDir")).toString());
}

void OptionsDialog::save() {
    QSettings s;
    s.setValue(QStringLiteral("id/nick"), m_nick->text());
    s.setValue(QStringLiteral("id/altNick"), m_altNick->text());
    s.setValue(QStringLiteral("id/user"), m_user->text());
    s.setValue(QStringLiteral("id/realname"), m_realname->text());
    const QString theme = m_theme->currentData().toString();
    s.setValue(QStringLiteral("theme"), theme);
    s.setValue(QStringLiteral("display/timestamps"), m_timestamps->isChecked());
    s.setValue(QStringLiteral("display/timeFormat"), m_timeFormat->currentText());
    s.setValue(QStringLiteral("display/scrollback"), m_scrollback->value());
    s.setValue(QStringLiteral("display/stripColors"), m_stripColors->isChecked());
    s.setValue(QStringLiteral("irc/logging"), m_logging->isChecked());
    s.setValue(QStringLiteral("irc/logDir"), m_logDir->text());
    applyTheme(*qApp, theme);  // theme takes effect immediately
}

}  // namespace lurker::gui
