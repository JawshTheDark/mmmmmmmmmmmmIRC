// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The Options dialog — a faithful recreation of mIRC's Options layout: a
// category tree on the left and a stack of setting pages on the right
// (Connect, IRC, DCC, Display, Colors, Sound, Mouse, Logging, Other). Every
// control is bound to a QSettings key and persisted; pages whose features are
// already live take effect immediately (theme, colours), the rest are stored
// so behaviour lands as each feature is wired.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include <functional>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace lurker::gui {

class OptionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit OptionsDialog(QWidget* parent = nullptr);

    // Reveal a page by its tree label (e.g. "Colors"); no-op if not found.
    void selectPage(const QString& name);

private:
    // A widget bound to a settings key. The widget's concrete type is detected
    // at load/save time (QLineEdit, QCheckBox, QSpinBox, QComboBox).
    struct Binding {
        QWidget* widget = nullptr;
        QString key;
        QVariant def;
        bool useData = false;  // combo: persist currentData() instead of text
    };

    void load();
    void save();

    // Page builders return a fresh page widget whose controls are already bound.
    QWidget* buildPage(const QString& title, const QString& blurb,
                       const std::function<void(QFormLayout*)>& body);

    // Binding helpers: create a control, register it, return it for layout.
    QLineEdit* boundEdit(const QString& key, const QString& def = QString());
    QCheckBox* boundCheck(const QString& label, const QString& key, bool def);
    QSpinBox* boundSpin(const QString& key, int lo, int hi, int def, int step = 1);
    QComboBox* boundCombo(const QString& key, const QStringList& items, const QString& def);

    QTreeWidgetItem* addCategory(QTreeWidgetItem* parent, const QString& name, QWidget* page);

    QTreeWidget* m_tree = nullptr;
    QStackedWidget* m_stack = nullptr;
    QVector<Binding> m_bindings;
};

}  // namespace lurker::gui
