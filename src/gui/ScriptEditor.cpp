// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ScriptEditor.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace lurker::gui {

ScriptEditor::ScriptEditor(QWidget* parent, std::function<QStringList()> files, std::function<void()> reload,
                           std::function<QString()> summary)
    : QDialog(parent), m_files(std::move(files)), m_reload(std::move(reload)), m_summary(std::move(summary)) {
    setWindowTitle(tr("Scripts Editor"));
    resize(820, 560);

    auto* outer = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);

    m_list = new QListWidget(split);
    m_list->setMaximumWidth(240);
    split->addWidget(m_list);

    m_editor = new QPlainTextEdit(split);
    m_editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    split->addWidget(m_editor);
    split->setStretchFactor(1, 1);
    outer->addWidget(split, 1);

    m_status = new QLabel(this);
    outer->addWidget(m_status);

    auto* buttons = new QDialogButtonBox(this);
    auto* saveBtn = buttons->addButton(tr("Save && Reload"), QDialogButtonBox::ApplyRole);
    auto* reloadBtn = buttons->addButton(tr("Reload All"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Close);
    outer->addWidget(buttons);

    connect(m_list, &QListWidget::currentTextChanged, this, [this](const QString& t) { showFile(t); });
    connect(saveBtn, &QPushButton::clicked, this, &ScriptEditor::saveCurrent);
    connect(reloadBtn, &QPushButton::clicked, this, [this] {
        if (m_reload) m_reload();
        if (m_summary) m_status->setText(m_summary());
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    loadList();
}

void ScriptEditor::loadList() {
    m_list->clear();
    for (const QString& f : m_files()) m_list->addItem(f);
    if (m_list->count()) m_list->setCurrentRow(0);
    if (m_summary) m_status->setText(m_summary());
}

void ScriptEditor::showFile(const QString& path) {
    m_current = path;
    QFile f(path);
    m_editor->setPlainText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
    m_editor->setReadOnly(path.isEmpty());
}

void ScriptEditor::saveCurrent() {
    if (m_current.isEmpty()) return;
    QFile f(m_current);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QByteArray data = m_editor->toPlainText().toUtf8();
        f.write(data);
        f.close();
    }
    if (m_reload) m_reload();
    if (m_summary) m_status->setText(tr("Saved %1 — %2").arg(QFileInfo(m_current).fileName(), m_summary()));
}

}  // namespace lurker::gui
