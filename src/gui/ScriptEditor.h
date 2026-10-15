// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The Scripts Editor (Alt+R), styled after mIRC's: a menu bar (File/Edit/View),
// one tab per loaded .mrc, MSL syntax highlighting, and find/replace. Saving a
// tab writes the file and reloads the engine; New/Open add a file to the loaded
// set. A status line shows the engine's alias/event/dialog counts.
#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QWidget;

namespace lurker::gui {

class ScriptEditor : public QDialog {
    Q_OBJECT
public:
    ScriptEditor(QWidget* parent, std::function<QStringList()> files, std::function<void()> reload,
                 std::function<QString()> summary, std::function<void(const QString&)> loadFile);

private:
    void rebuildTabs();
    void openTab(const QString& path);
    QPlainTextEdit* currentEditor() const;
    QString currentPath() const;
    void saveCurrent();
    void saveAll();
    void newFile();
    void openFile();
    void toggleFind(bool withReplace);
    void findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void refreshStatus(const QString& prefix = QString());

    std::function<QStringList()> m_files;
    std::function<void()> m_reload;
    std::function<QString()> m_summary;
    std::function<void(const QString&)> m_loadFile;

    QTabWidget* m_tabs = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
};

}  // namespace lurker::gui
