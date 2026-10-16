// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The DCC Transfers window: Lurker's download manager (/api/dcc) rendered as a
// table — direction, peer, file, size, live progress and state. The toolbar acts
// on the selected row (accept/reject a pending offer, cancel an in-flight one,
// save a completed file locally, or delete the server copy). Rows are seeded from
// the initial list and updated live from `dcc-transfer` frames.
#pragma once

#include <QHash>
#include <QWidget>

#include "backend/ChatBackend.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace lurker::gui {

class DccTransfersWindow : public QWidget {
    Q_OBJECT
public:
    explicit DccTransfersWindow(QWidget* parent = nullptr);

    void setTransfers(const QList<lurker::backend::DccTransfer>& transfers);
    void upsert(const lurker::backend::DccTransfer& t);

signals:
    void sendRequested();  // user clicked "Send File…"
    void acceptRequested(int id);
    void rejectRequested(int id);
    void cancelRequested(int id);
    void saveRequested(int id, const QString& suggestedName);
    void deleteRequested(int id);

private:
    int selectedId() const;
    void updateButtons();
    void writeRow(int row, const lurker::backend::DccTransfer& t);

    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
    QPushButton* m_send = nullptr;
    QPushButton* m_accept = nullptr;
    QPushButton* m_reject = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_delete = nullptr;
    QHash<int, int> m_rowById;  // transfer id -> table row
    QHash<int, lurker::backend::DccTransfer> m_byId;
};

}  // namespace lurker::gui
