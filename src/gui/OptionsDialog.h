// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The Options dialog — a tabbed editor over the app's settings (identity,
// display, IRC behaviour), persisted with QSettings. A first slice of mIRC's
// Options tree; more panes are added as features land. Settings other panes
// read live are noted inline.
#pragma once

#include <QDialog>

class QApplication;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace lurker::gui {

class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(QWidget* parent = nullptr);

private:
    void load();
    void save();

    QLineEdit* m_nick = nullptr;
    QLineEdit* m_altNick = nullptr;
    QLineEdit* m_user = nullptr;
    QLineEdit* m_realname = nullptr;
    QComboBox* m_theme = nullptr;
    QCheckBox* m_timestamps = nullptr;
    QSpinBox* m_scrollback = nullptr;
    QComboBox* m_timeFormat = nullptr;
    QCheckBox* m_logging = nullptr;
    QLineEdit* m_logDir = nullptr;
    QCheckBox* m_stripColors = nullptr;
};

}  // namespace lurker::gui
