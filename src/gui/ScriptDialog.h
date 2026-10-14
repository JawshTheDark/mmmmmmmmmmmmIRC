// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// A real Qt dialog built from an MSL DialogTable. Controls are placed by their
// dialog-unit geometry, keyed by id; /did commands mutate them, $did queries
// them, and control events (clicks, edits) are reported back so `on DIALOG`
// handlers fire. One ScriptDialog per open dialog instance.
#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <functional>

#include "core/msl/Dialog.h"

class QWidget;

namespace lurker::gui {

class ScriptDialog : public QDialog {
    Q_OBJECT
public:
    // eventSink is called with (devent, id) for sclick/edit/close/... so the
    // owner can fire the matching on DIALOG handlers.
    ScriptDialog(const lurker::msl::DialogTable& table, QString instance,
                 std::function<void(const QString&, int)> eventSink, QWidget* parent = nullptr);

    const QString& instanceName() const { return m_instance; }

    // /did: apply a command to a control (switches without the leading '-').
    void applyDid(int id, const QString& switches, const QString& text);
    // $did(name,id).prop
    QString query(int id, const QString& prop) const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QString m_instance;
    std::function<void(const QString&, int)> m_sink;
    QHash<int, QWidget*> m_byId;
    bool m_closeFired = false;
};

}  // namespace lurker::gui
