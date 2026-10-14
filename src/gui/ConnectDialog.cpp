// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ConnectDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>

namespace lurker::gui {

ConnectDialog::ConnectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Connect to Lurker"));
    setModal(true);

    auto* form = new QFormLayout(this);
    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(QStringLiteral("chat.example.com"));
    m_port = new QLineEdit(QStringLiteral("1025"), this);
    m_tls = new QCheckBox(tr("Use TLS"), this);
    m_tls->setChecked(true);
    m_pin = new QLineEdit(this);
    m_pin->setPlaceholderText(tr("optional SHA-256 fingerprint to pin"));
    m_nick = new QLineEdit(this);
    m_username = new QLineEdit(this);
    m_network = new QComboBox(this);
    m_network->setEditable(true);
    m_network->setPlaceholderText(tr("(optional) network to bind"));
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("account password or API token"));

    form->addRow(tr("Host"), m_host);
    form->addRow(tr("Port"), m_port);
    form->addRow(QString(), m_tls);
    form->addRow(tr("Pin cert"), m_pin);
    form->addRow(tr("Nick"), m_nick);
    form->addRow(tr("Username"), m_username);
    form->addRow(tr("Network"), m_network);
    form->addRow(tr("Secret"), m_password);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadDevProfile();
}

void ConnectDialog::loadDevProfile() {
    // Search upward for .secrets/dev-profile.json so the dialog is prefilled
    // whether the app is run from the source tree or the build directory.
    QDir dir(QDir::currentPath());
    QString path;
    for (int i = 0; i < 5; ++i) {
        const QString candidate = dir.filePath(QStringLiteral(".secrets/dev-profile.json"));
        if (QFile::exists(candidate)) {
            path = candidate;
            break;
        }
        if (!dir.cdUp()) break;
    }
    if (path.isEmpty()) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (o.contains(QStringLiteral("host"))) m_host->setText(o.value(QStringLiteral("host")).toString());
    if (o.contains(QStringLiteral("bouncerPort")))
        m_port->setText(QString::number(o.value(QStringLiteral("bouncerPort")).toInt()));
    if (o.contains(QStringLiteral("tls"))) m_tls->setChecked(o.value(QStringLiteral("tls")).toBool(true));
    if (o.contains(QStringLiteral("certSha256"))) m_pin->setText(o.value(QStringLiteral("certSha256")).toString());
    if (o.contains(QStringLiteral("nick"))) m_nick->setText(o.value(QStringLiteral("nick")).toString());
    if (o.contains(QStringLiteral("username"))) m_username->setText(o.value(QStringLiteral("username")).toString());
    for (const QJsonValue& v : o.value(QStringLiteral("networks")).toArray()) m_network->addItem(v.toString());
    if (o.contains(QStringLiteral("defaultNetwork")))
        m_network->setCurrentText(o.value(QStringLiteral("defaultNetwork")).toString());

    // Dev convenience only: load the token from the file the profile names so a
    // local run connects without retyping. Never persisted from here.
    const QString tokenRel = o.value(QStringLiteral("tokenFile")).toString();
    if (!tokenRel.isEmpty()) {
        const QString tokenPath = QFileInfo(path).dir().filePath(QFileInfo(tokenRel).fileName());
        QFile tf(tokenPath);
        if (tf.open(QIODevice::ReadOnly)) m_password->setText(QString::fromUtf8(tf.readAll()).trimmed());
    }
}

namespace {
// Locate .secrets/dev-profile.json by walking up from the working directory.
QString findDevProfile() {
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath(QStringLiteral(".secrets/dev-profile.json"));
        if (QFile::exists(candidate)) return candidate;
        if (!dir.cdUp()) break;
    }
    return {};
}
}  // namespace

bool devProfileSettings(lurker::backend::ConnectSettings& out) {
    const QString path = findDevProfile();
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

    out.host = o.value(QStringLiteral("host")).toString();
    out.port = static_cast<quint16>(o.value(QStringLiteral("bouncerPort")).toInt(1025));
    out.tls = o.value(QStringLiteral("tls")).toBool(true);
    out.pinnedSha256 = o.value(QStringLiteral("certSha256")).toString();
    out.nick = o.value(QStringLiteral("nick")).toString();
    out.realname = QStringLiteral("Lurker Desktop");
    QString user = o.value(QStringLiteral("username")).toString();
    const QString network = o.value(QStringLiteral("defaultNetwork")).toString();
    if (!network.isEmpty()) user = user + QLatin1Char('/') + network;
    out.username = user;

    const QString tokenRel = o.value(QStringLiteral("tokenFile")).toString();
    if (!tokenRel.isEmpty()) {
        const QString tokenPath = QFileInfo(path).dir().filePath(QFileInfo(tokenRel).fileName());
        QFile tf(tokenPath);
        if (tf.open(QIODevice::ReadOnly)) out.password = QString::fromUtf8(tf.readAll()).trimmed();
    }
    return !out.host.isEmpty() && !out.nick.isEmpty();
}

lurker::backend::ConnectSettings ConnectDialog::settings() const {
    lurker::backend::ConnectSettings s;
    s.host = m_host->text().trimmed();
    s.port = static_cast<quint16>(m_port->text().toUInt());
    s.tls = m_tls->isChecked();
    s.pinnedSha256 = m_pin->text().trimmed();
    s.nick = m_nick->text().trimmed();
    s.realname = QStringLiteral("Lurker Desktop");
    s.password = m_password->text();

    QString user = m_username->text().trimmed();
    const QString network = m_network->currentText().trimmed();
    if (!network.isEmpty()) user = user + QLatin1Char('/') + network;
    s.username = user;
    return s;
}

}  // namespace lurker::gui
