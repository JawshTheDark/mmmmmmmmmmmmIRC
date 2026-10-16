// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Connect dialog. Offers the two backends — Lurker (WebSocket/REST, every
// network at once) and Bouncer (raw IRC, one network) — and builds a connected
// ChatBackend. For development it prefills from .secrets/dev-profile.json when
// present; nothing secret is written back out. The full connection manager
// (saved networks, keychain storage) is WP 1.5.
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QStackedWidget;

namespace lurker::backend {
class ChatBackend;
}

namespace lurker::gui {

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(QWidget* parent = nullptr);

    // Creates the chosen backend and begins connecting. Caller takes ownership
    // (passes it to MainWindow::useBackend, which reparents it).
    lurker::backend::ChatBackend* createBackend(QObject* parent) const;

    // A stable key for "this server + account" (mode|host|user), so the caller can
    // warn before opening a second connection to a Lurker server it already holds.
    QString identity() const;

    // Build a connected backend from the last-saved connection (QSettings) for
    // auto-connect at launch. Returns nullptr if no server or no stored creds.
    static lurker::backend::ChatBackend* createBackendFromSettings(QObject* parent);

private:
    void loadDevProfile();
    void loadSettings();  // restore the last-used connection from QSettings
    void saveSettings();  // persist the connection (secrets only if "remember")

    QComboBox* m_mode = nullptr;  // 0 = Lurker (WS), 1 = Bouncer (raw IRC)
    QLineEdit* m_host = nullptr;
    QLineEdit* m_port = nullptr;  // bouncer only
    QCheckBox* m_tls = nullptr;
    QLineEdit* m_pin = nullptr;
    QLineEdit* m_nick = nullptr;      // bouncer
    QLineEdit* m_username = nullptr;
    QComboBox* m_network = nullptr;   // bouncer: user/network binding
    QLineEdit* m_password = nullptr;  // secret (bouncer) or password-to-mint (Lurker)
    QLineEdit* m_token = nullptr;     // Lurker: a minted/OAuth token (optional)
    QCheckBox* m_remember = nullptr;  // persist the password/token too
};

}  // namespace lurker::gui
