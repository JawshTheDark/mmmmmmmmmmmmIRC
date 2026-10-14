// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// A minimal Scripts Editor: the loaded .mrc files on the left, an editor on the
// right, Save & Reload / Reload All, and a status line with the engine's current
// alias/event counts. mIRC's tabbed editor (Aliases/Popups/Remotes/...) is the
// fuller version; this manages whole files, which is how scripts load here.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>

class QListWidget;
class QPlainTextEdit;
class QLabel;

namespace lurker::gui {

class ScriptEditor : public QDialog {
    Q_OBJECT
public:
    ScriptEditor(QWidget* parent, std::function<QStringList()> files, std::function<void()> reload,
                 std::function<QString()> summary);

private:
    void loadList();
    void showFile(const QString& path);
    void saveCurrent();

    std::function<QStringList()> m_files;
    std::function<void()> m_reload;
    std::function<QString()> m_summary;
    QListWidget* m_list = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QLabel* m_status = nullptr;
    QString m_current;
};

}  // namespace lurker::gui
