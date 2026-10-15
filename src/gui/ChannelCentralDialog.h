// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Channel Central: manage one channel's topic, modes, key/limit and ban list in
// one place. A clean-room functional equivalent of mIRC's channel manager —
// same capabilities, our own layout and standard IRC terms. The owner wires the
// signals to the backend; current state is filled from the MODE/ban numerics.
#pragma once

#include <QDialog>
#include <QHash>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

namespace lurker::gui {

class ChannelCentralDialog : public QDialog {
    Q_OBJECT
public:
    ChannelCentralDialog(int net, const QString& channel, QWidget* parent = nullptr);

    int network() const { return m_net; }
    const QString& channel() const { return m_channel; }

    void setTopicText(const QString& topic);              // from RPL_TOPIC (332)
    void setCurrentModes(const QString& modes, const QString& key, int limit);  // from RPL_CHANNELMODEIS (324)
    void setBanList(const QStringList& masks);            // replace (from a 367 burst)
    void addBanMask(const QString& mask);                 // one live 367 row

signals:
    void topicChangeRequested(int net, const QString& channel, const QString& topic);
    void modeChangeRequested(int net, const QString& channel, const QString& modeString);
    void stateRefreshRequested(int net, const QString& channel);  // owner should send MODE / MODE +b

private:
    void applyModes();
    QString m_channel;
    int m_net = -1;

    QLineEdit* m_topic = nullptr;
    QHash<QChar, QCheckBox*> m_modeChecks;  // mode letter -> checkbox
    QCheckBox* m_keyOn = nullptr;
    QLineEdit* m_key = nullptr;
    QCheckBox* m_limitOn = nullptr;
    QSpinBox* m_limit = nullptr;
    QListWidget* m_bans = nullptr;
    QLabel* m_status = nullptr;

    // Current server-side state, to compute the minimal MODE delta on Apply.
    QString m_curModes;
    QString m_curKey;
    int m_curLimit = 0;
};

}  // namespace lurker::gui
