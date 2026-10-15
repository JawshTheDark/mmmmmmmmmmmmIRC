// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ConnectDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>

#include "backend/BouncerChatBackend.h"
#include "backend/LurkerChatBackend.h"

namespace lurker::gui {

namespace {
QString findDevProfile() {
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 6; ++i) {
        const QString candidate = dir.filePath(QStringLiteral(".secrets/dev-profile.json"));
        if (QFile::exists(candidate)) return candidate;
        if (!dir.cdUp()) break;
    }
    return {};
}
QString readTrimmed(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}
}  // namespace

ConnectDialog::ConnectDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Connect to Lurker"));
    setModal(true);

    auto* form = new QFormLayout(this);
    m_mode = new QComboBox(this);
    m_mode->addItems({tr("Lurker (WebSocket — all networks)"), tr("Bouncer (raw IRC — one network)")});
    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(QStringLiteral("chat.example.com"));
    m_port = new QLineEdit(QStringLiteral("1025"), this);
    m_tls = new QCheckBox(tr("Use TLS"), this);
    m_tls->setChecked(true);
    m_pin = new QLineEdit(this);
    m_pin->setPlaceholderText(tr("optional SHA-256 fingerprint to pin (bouncer)"));
    m_nick = new QLineEdit(this);
    m_username = new QLineEdit(this);
    m_network = new QComboBox(this);
    m_network->setEditable(true);
    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("password (Lurker: to mint a token; Bouncer: secret/token)"));
    m_token = new QLineEdit(this);
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setPlaceholderText(tr("Lurker: existing minted/OAuth token (optional)"));
    m_remember = new QCheckBox(tr("Remember password / token on this computer"), this);

    form->addRow(tr("Connect via"), m_mode);
    form->addRow(tr("Host"), m_host);
    form->addRow(tr("Port"), m_port);
    form->addRow(QString(), m_tls);
    form->addRow(tr("Pin cert"), m_pin);
    form->addRow(tr("Nick"), m_nick);
    form->addRow(tr("Username"), m_username);
    form->addRow(tr("Network"), m_network);
    form->addRow(tr("Password"), m_password);
    form->addRow(tr("Token"), m_token);
    form->addRow(QString(), m_remember);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveSettings();  // persist what was entered before we connect
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto updateRows = [this] {
        const bool bouncer = m_mode->currentIndex() == 1;
        m_port->setText(bouncer ? QStringLiteral("1025") : QStringLiteral("443"));
        m_token->setEnabled(!bouncer);
    };
    connect(m_mode, &QComboBox::currentIndexChanged, this, [updateRows](int) { updateRows(); });

    loadDevProfile();   // dev convenience (.secrets/dev-profile.json), if present
    loadSettings();     // the user's last-used connection takes precedence
    // Saved identity (Options) fills anything still empty.
    QSettings id;
    if (m_nick->text().isEmpty()) m_nick->setText(id.value(QStringLiteral("id/nick")).toString());
    if (m_username->text().isEmpty()) m_username->setText(id.value(QStringLiteral("id/user")).toString());
    updateRows();
}

void ConnectDialog::loadSettings() {
    QSettings s;
    s.beginGroup(QStringLiteral("connect"));
    if (!s.contains(QStringLiteral("host"))) {  // nothing saved yet
        s.endGroup();
        return;
    }
    m_mode->setCurrentIndex(s.value(QStringLiteral("mode"), 0).toInt());
    m_host->setText(s.value(QStringLiteral("host")).toString());
    if (s.contains(QStringLiteral("port"))) m_port->setText(s.value(QStringLiteral("port")).toString());
    m_tls->setChecked(s.value(QStringLiteral("tls"), true).toBool());
    m_pin->setText(s.value(QStringLiteral("pin")).toString());
    if (!s.value(QStringLiteral("nick")).toString().isEmpty()) m_nick->setText(s.value(QStringLiteral("nick")).toString());
    if (!s.value(QStringLiteral("username")).toString().isEmpty())
        m_username->setText(s.value(QStringLiteral("username")).toString());
    const QString net = s.value(QStringLiteral("network")).toString();
    if (!net.isEmpty()) m_network->setCurrentText(net);
    const bool remember = s.value(QStringLiteral("remember"), false).toBool();
    m_remember->setChecked(remember);
    if (remember) {
        if (!s.value(QStringLiteral("password")).toString().isEmpty())
            m_password->setText(s.value(QStringLiteral("password")).toString());
        if (!s.value(QStringLiteral("token")).toString().isEmpty())
            m_token->setText(s.value(QStringLiteral("token")).toString());
    }
    s.endGroup();
}

void ConnectDialog::saveSettings() {
    QSettings s;
    s.beginGroup(QStringLiteral("connect"));
    s.setValue(QStringLiteral("mode"), m_mode->currentIndex());
    s.setValue(QStringLiteral("host"), m_host->text().trimmed());
    s.setValue(QStringLiteral("port"), m_port->text().trimmed());
    s.setValue(QStringLiteral("tls"), m_tls->isChecked());
    s.setValue(QStringLiteral("pin"), m_pin->text().trimmed());
    s.setValue(QStringLiteral("nick"), m_nick->text().trimmed());
    s.setValue(QStringLiteral("username"), m_username->text().trimmed());
    s.setValue(QStringLiteral("network"), m_network->currentText().trimmed());
    const bool remember = m_remember->isChecked();
    s.setValue(QStringLiteral("remember"), remember);
    // Only keep secrets when asked; otherwise clear any previously-stored ones.
    s.setValue(QStringLiteral("password"), remember ? m_password->text() : QString());
    s.setValue(QStringLiteral("token"), remember ? m_token->text() : QString());
    s.endGroup();
}

void ConnectDialog::loadDevProfile() {
    const QString path = findDevProfile();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QDir dir = QFileInfo(path).dir();

    m_host->setText(o.value(QStringLiteral("host")).toString());
    m_tls->setChecked(o.value(QStringLiteral("tls")).toBool(true));
    m_pin->setText(o.value(QStringLiteral("certSha256")).toString());
    m_nick->setText(o.value(QStringLiteral("nick")).toString());
    m_username->setText(o.value(QStringLiteral("username")).toString());
    for (const QJsonValue& v : o.value(QStringLiteral("networks")).toArray()) m_network->addItem(v.toString());
    m_network->setCurrentText(o.value(QStringLiteral("defaultNetwork")).toString());

    const QJsonObject ws = o.value(QStringLiteral("ws")).toObject();
    if (ws.contains(QStringLiteral("wsTokenFile")))
        m_token->setText(readTrimmed(dir.filePath(QFileInfo(ws.value(QStringLiteral("wsTokenFile")).toString()).fileName())));
    if (ws.contains(QStringLiteral("passwordFile")))
        m_password->setText(readTrimmed(dir.filePath(QFileInfo(ws.value(QStringLiteral("passwordFile")).toString()).fileName())));
}

lurker::backend::ChatBackend* ConnectDialog::createBackend(QObject* parent) const {
    const bool bouncer = m_mode->currentIndex() == 1;
    if (bouncer) {
        auto* b = new lurker::backend::BouncerChatBackend(parent);
        lurker::backend::ConnectSettings s;
        s.host = m_host->text().trimmed();
        s.port = static_cast<quint16>(m_port->text().toUInt());
        s.tls = m_tls->isChecked();
        s.pinnedSha256 = m_pin->text().trimmed();
        s.nick = m_nick->text().trimmed();
        s.realname = QStringLiteral("Lurker Desktop");
        s.password = m_password->text();
        QString user = m_username->text().trimmed();
        const QString net = m_network->currentText().trimmed();
        if (!net.isEmpty()) user += QLatin1Char('/') + net;
        s.username = user;
        b->connectToHost(s);
        return b;
    }
    auto* b = new lurker::backend::LurkerChatBackend(parent);
    lurker::backend::LurkerConnectSettings s;
    s.host = m_host->text().trimmed();
    s.tls = m_tls->isChecked();
    s.token = m_token->text();
    s.username = m_username->text().trimmed();
    s.password = m_password->text();
    b->connectToLurker(s);
    return b;
}

}  // namespace lurker::gui
