// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The Scripts Editor (Alt+R), laid out like mIRC's: section tabs for Aliases,
// Popups, Remotes, Users and Variables. Each editable section is backed by a
// dedicated .mrc under the app's scripts/ dir (auto-created and auto-loaded, so
// what you type there works and persists); Variables shows the live %globals.
// Any other loaded scripts appear as extra tabs. A File/Edit menu bar, MSL
// syntax highlighting and find/replace round it out.
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
                 std::function<QString()> summary, std::function<void(const QString&)> loadFile,
                 std::function<QString()> variables);

    // The dedicated section files (Aliases, Popups, Remotes, Users), in order,
    // under <app-data>/scripts. MainWindow ensures + loads these at startup.
    static QStringList sectionFiles();
    static QString sectionDir();

private:
    void rebuildTabs();
    QPlainTextEdit* addEditorTab(const QString& label, const QString& path);
    QPlainTextEdit* currentEditor() const;
    QString currentPath() const;
    void saveCurrent();
    void saveAll();
    void openFile();
    void refreshVariables();
    void toggleFind(bool withReplace);
    void findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void refreshStatus(const QString& prefix = QString());

    std::function<QStringList()> m_files;
    std::function<void()> m_reload;
    std::function<QString()> m_summary;
    std::function<void(const QString&)> m_loadFile;
    std::function<QString()> m_variables;

    QTabWidget* m_tabs = nullptr;
    QPlainTextEdit* m_varsView = nullptr;
    QLabel* m_status = nullptr;
    QWidget* m_findBar = nullptr;
    QLineEdit* m_findEdit = nullptr;
    QLineEdit* m_replaceEdit = nullptr;
};

}  // namespace lurker::gui
