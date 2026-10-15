// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ScriptEditor.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QVBoxLayout>

namespace lurker::gui {

namespace {

// Lightweight MSL syntax highlighter: comments, section/control keywords,
// $identifiers and %variables. No Q_OBJECT needed (only highlightBlock).
class MslHighlighter : public QSyntaxHighlighter {
public:
    explicit MslHighlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {
        const bool dark = QApplication::palette().color(QPalette::Base).lightness() < 128;
        kw.setForeground(dark ? QColor(0x6C, 0xB6, 0xFF) : QColor(0x00, 0x55, 0xAA));
        kw.setFontWeight(QFont::Bold);
        ident.setForeground(dark ? QColor(0x5A, 0xD6, 0xC0) : QColor(0x00, 0x80, 0x80));
        var.setForeground(dark ? QColor(0xD6, 0x9C, 0xF0) : QColor(0x90, 0x00, 0x90));
        comment.setForeground(dark ? QColor(0x8A, 0xA0, 0x8A) : QColor(0x50, 0x80, 0x50));
        comment.setFontItalic(true);
        reKw.setPattern(QStringLiteral("^\\s*(on|alias|menu|dialog|ctcp|raw|scon|scid)\\b|"
                                       "\\b(if|elseif|else|while|var|set|unset|inc|dec|goto|return|halt|"
                                       "haltdef|break|continue|tokenize)\\b"));
        reKw.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        reIdent.setPattern(QStringLiteral("\\$\\+?[A-Za-z_][A-Za-z0-9_]*"));
        reVar.setPattern(QStringLiteral("%[A-Za-z0-9_.]+"));
    }

protected:
    void highlightBlock(const QString& text) override {
        auto apply = [&](const QRegularExpression& re, const QTextCharFormat& fmt) {
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                const auto m = it.next();
                setFormat(m.capturedStart(), m.capturedLength(), fmt);
            }
        };
        apply(reKw, kw);
        apply(reIdent, ident);
        apply(reVar, var);
        const int semi = text.indexOf(QLatin1Char(';'));
        if (semi >= 0) setFormat(semi, text.length() - semi, comment);
    }

private:
    QTextCharFormat kw, ident, var, comment;
    QRegularExpression reKw, reIdent, reVar;
};

}  // namespace

QString ScriptEditor::sectionDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/scripts");
}

QStringList ScriptEditor::sectionFiles() {
    const QString d = sectionDir();
    return {d + QStringLiteral("/aliases.mrc"), d + QStringLiteral("/popups.mrc"),
            d + QStringLiteral("/remote.mrc"), d + QStringLiteral("/users.mrc")};
}

ScriptEditor::ScriptEditor(QWidget* parent, std::function<QStringList()> files, std::function<void()> reload,
                           std::function<QString()> summary, std::function<void(const QString&)> loadFile,
                           std::function<QString()> variables)
    : QDialog(parent),
      m_files(std::move(files)),
      m_reload(std::move(reload)),
      m_summary(std::move(summary)),
      m_loadFile(std::move(loadFile)),
      m_variables(std::move(variables)) {
    setWindowTitle(tr("Scripts Editor"));
    resize(920, 620);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* menubar = new QMenuBar(this);
    outer->setMenuBar(menubar);
    auto* fileMenu = menubar->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open Script..."), QKeySequence::Open, this, &ScriptEditor::openFile);
    fileMenu->addAction(tr("&Save"), QKeySequence::Save, this, &ScriptEditor::saveCurrent);
    fileMenu->addAction(tr("Save &All"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), this, &ScriptEditor::saveAll);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Reload All"), QKeySequence(QStringLiteral("F5")), this, [this] {
        if (m_reload) m_reload();
        refreshVariables();
        refreshStatus(tr("Reloaded"));
    });
    fileMenu->addAction(tr("&Close"), QKeySequence(QStringLiteral("Ctrl+W")), this, &QDialog::close);

    auto* editMenu = menubar->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Undo"), QKeySequence::Undo, this, [this] {
        if (auto* e = currentEditor()) e->undo();
    });
    editMenu->addAction(tr("&Redo"), QKeySequence::Redo, this, [this] {
        if (auto* e = currentEditor()) e->redo();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Select &All"), QKeySequence::SelectAll, this, [this] {
        if (auto* e = currentEditor()) e->selectAll();
    });
    editMenu->addAction(tr("&Find..."), QKeySequence::Find, this, [this] { toggleFind(false); });
    editMenu->addAction(tr("&Replace..."), QKeySequence::Replace, this, [this] { toggleFind(true); });
    editMenu->addAction(tr("Find &Next"), QKeySequence::FindNext, this, [this] { findNext(false); });

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    outer->addWidget(m_tabs, 1);

    // Find / replace bar.
    m_findBar = new QWidget(this);
    auto* fb = new QHBoxLayout(m_findBar);
    fb->setContentsMargins(6, 2, 6, 2);
    m_findEdit = new QLineEdit(m_findBar);
    m_findEdit->setPlaceholderText(tr("Find"));
    m_replaceEdit = new QLineEdit(m_findBar);
    m_replaceEdit->setPlaceholderText(tr("Replace with"));
    auto* nextBtn = new QPushButton(tr("Next"), m_findBar);
    auto* prevBtn = new QPushButton(tr("Prev"), m_findBar);
    auto* repBtn = new QPushButton(tr("Replace"), m_findBar);
    auto* repAllBtn = new QPushButton(tr("All"), m_findBar);
    auto* closeBtn = new QPushButton(tr("×"), m_findBar);
    closeBtn->setMaximumWidth(28);
    fb->addWidget(m_findEdit, 2);
    fb->addWidget(nextBtn);
    fb->addWidget(prevBtn);
    fb->addWidget(m_replaceEdit, 2);
    fb->addWidget(repBtn);
    fb->addWidget(repAllBtn);
    fb->addWidget(closeBtn);
    m_findBar->hide();
    outer->addWidget(m_findBar);
    connect(nextBtn, &QPushButton::clicked, this, [this] { findNext(false); });
    connect(prevBtn, &QPushButton::clicked, this, [this] { findNext(true); });
    connect(m_findEdit, &QLineEdit::returnPressed, this, [this] { findNext(false); });
    connect(repBtn, &QPushButton::clicked, this, &ScriptEditor::replaceOne);
    connect(repAllBtn, &QPushButton::clicked, this, &ScriptEditor::replaceAll);
    connect(closeBtn, &QPushButton::clicked, this, [this] { m_findBar->hide(); });

    m_status = new QLabel(this);
    m_status->setContentsMargins(6, 2, 6, 4);
    outer->addWidget(m_status);

    rebuildTabs();
}

QPlainTextEdit* ScriptEditor::addEditorTab(const QString& label, const QString& path) {
    auto* ed = new QPlainTextEdit;
    ed->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    ed->setLineWrapMode(QPlainTextEdit::NoWrap);
    ed->setTabStopDistance(ed->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 2);
    ed->setProperty("path", path);
    QFile f(path);
    ed->setPlainText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
    new MslHighlighter(ed->document());
    const int idx = m_tabs->addTab(ed, label);
    m_tabs->setTabToolTip(idx, path);
    return ed;
}

void ScriptEditor::rebuildTabs() {
    const QString keep = currentPath();
    m_tabs->clear();

    // mIRC's section tabs, each backed by a dedicated file.
    const QStringList sections = sectionFiles();
    const QStringList labels = {tr("Aliases"), tr("Popups"), tr("Remotes"), tr("Users")};
    for (int i = 0; i < sections.size(); ++i) addEditorTab(labels[i], sections[i]);

    // Variables: a live, read-only view of the engine's %globals.
    m_varsView = new QPlainTextEdit;
    m_varsView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_varsView->setReadOnly(true);
    m_tabs->addTab(m_varsView, tr("Variables"));
    refreshVariables();

    // Any other loaded scripts (e.g. imported .mrc) as extra tabs.
    for (const QString& f : m_files()) {
        if (sections.contains(f)) continue;
        addEditorTab(QFileInfo(f).fileName(), f);
    }

    // Restore the previously-focused document if it still exists.
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabToolTip(i) == keep) {
            m_tabs->setCurrentIndex(i);
            break;
        }
    }
    refreshStatus();
}

void ScriptEditor::refreshVariables() {
    if (!m_varsView) return;
    const QString v = m_variables ? m_variables() : QString();
    m_varsView->setPlainText(v.isEmpty() ? tr("; No %variables set.\n; Use /set %%name value or a script to define them.")
                                         : v);
}

QPlainTextEdit* ScriptEditor::currentEditor() const { return qobject_cast<QPlainTextEdit*>(m_tabs->currentWidget()); }

QString ScriptEditor::currentPath() const {
    auto* e = currentEditor();
    return e ? e->property("path").toString() : QString();
}

void ScriptEditor::saveCurrent() {
    auto* e = currentEditor();
    const QString path = currentPath();
    if (!e || path.isEmpty()) {
        refreshStatus(tr("Nothing to save (the Variables tab is read-only)"));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save failed"), tr("Could not write %1").arg(path));
        return;
    }
    f.write(e->toPlainText().toUtf8());
    f.close();
    if (m_reload) m_reload();
    refreshVariables();
    refreshStatus(tr("Saved %1").arg(QFileInfo(path).fileName()));
}

void ScriptEditor::saveAll() {
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto* e = qobject_cast<QPlainTextEdit*>(m_tabs->widget(i));
        const QString path = e ? e->property("path").toString() : QString();
        if (!e || e->isReadOnly() || path.isEmpty()) continue;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(e->toPlainText().toUtf8());
    }
    if (m_reload) m_reload();
    refreshVariables();
    refreshStatus(tr("Saved all"));
}

void ScriptEditor::openFile() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Script"), QString(),
                                                      tr("mIRC scripts (*.mrc);;All files (*)"));
    if (path.isEmpty()) return;
    if (m_loadFile) m_loadFile(path);
    rebuildTabs();
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabToolTip(i) == path) {
            m_tabs->setCurrentIndex(i);
            break;
        }
}

void ScriptEditor::toggleFind(bool withReplace) {
    m_replaceEdit->setVisible(withReplace);
    m_findBar->show();
    if (auto* e = currentEditor()) {
        const QString sel = e->textCursor().selectedText();
        if (!sel.isEmpty()) m_findEdit->setText(sel);
    }
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void ScriptEditor::findNext(bool backward) {
    auto* e = currentEditor();
    if (!e || m_findEdit->text().isEmpty()) return;
    QTextDocument::FindFlags flags;
    if (backward) flags |= QTextDocument::FindBackward;
    if (!e->find(m_findEdit->text(), flags)) {
        QTextCursor c = e->textCursor();
        c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        e->setTextCursor(c);
        e->find(m_findEdit->text(), flags);
    }
}

void ScriptEditor::replaceOne() {
    auto* e = currentEditor();
    if (!e || e->isReadOnly()) return;
    QTextCursor c = e->textCursor();
    if (c.hasSelection() && c.selectedText() == m_findEdit->text()) c.insertText(m_replaceEdit->text());
    findNext(false);
}

void ScriptEditor::replaceAll() {
    auto* e = currentEditor();
    if (!e || e->isReadOnly() || m_findEdit->text().isEmpty()) return;
    const QString doc = e->toPlainText();
    QString out = doc;
    out.replace(m_findEdit->text(), m_replaceEdit->text());
    if (out != doc) {
        e->selectAll();
        e->textCursor().insertText(out);
    }
}

void ScriptEditor::refreshStatus(const QString& prefix) {
    const QString s = m_summary ? m_summary() : QString();
    m_status->setText(prefix.isEmpty() ? s : prefix + QStringLiteral(" — ") + s);
}

}  // namespace lurker::gui
