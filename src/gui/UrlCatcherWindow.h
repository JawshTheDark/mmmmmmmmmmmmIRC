// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The URL Catcher: every http(s) link seen in chat, newest first, with where it
// came from. Double-click or Open launches it in the system browser; Copy puts
// it on the clipboard; Clear empties the list. mIRC's URL list, kept client-side.
#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;

namespace lurker::gui {

// One caught link and its context.
struct CaughtUrl {
    QString time;
    QString from;    // nick that said it
    QString target;  // channel / query it appeared in
    QString url;
};

class UrlCatcherWindow : public QWidget {
    Q_OBJECT
public:
    explicit UrlCatcherWindow(QWidget* parent = nullptr);

    void setUrls(const QList<CaughtUrl>& urls);  // replace (on open, from the kept history)
    void addUrl(const CaughtUrl& u);             // prepend one live

signals:
    void cleared();  // the user asked to empty the list (owner drops its history too)

private:
    void openSelected();
    void copySelected();
    QString selectedUrl() const;
    void insertRow(int row, const CaughtUrl& u);

    QTableWidget* m_table = nullptr;
    QLabel* m_status = nullptr;
};

}  // namespace lurker::gui
