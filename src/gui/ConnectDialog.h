// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Connect dialog for a Lurker bouncer. For development it prefills from
// .secrets/dev-profile.json (and reads the token from the file it names) when
// that exists; nothing secret is ever written back out. The full connection
// manager (saved networks, keychain storage) is WP 1.5.
#pragma once

#include <QDialog>

#include "backend/BouncerBackend.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

namespace lurker::gui {

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConnectDialog(QWidget* parent = nullptr);

    // The settings to connect with. The chosen network (if any) is appended to
    // the username as "user/network" for the bouncer to bind.
    lurker::backend::ConnectSettings settings() const;

private:
    void loadDevProfile();

    QLineEdit* m_host = nullptr;
    QLineEdit* m_port = nullptr;
    QCheckBox* m_tls = nullptr;
    QLineEdit* m_pin = nullptr;
    QLineEdit* m_nick = nullptr;
    QLineEdit* m_username = nullptr;
    QComboBox* m_network = nullptr;  // editable; "" => control connection
    QLineEdit* m_password = nullptr;
};

// Fills `out` directly from .secrets/dev-profile.json (username combined with
// defaultNetwork, token read from the file it names). Returns false when no
// profile is found. Used for headless dev runs that skip the dialog.
bool devProfileSettings(lurker::backend::ConnectSettings& out);

}  // namespace lurker::gui
