// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ChatView.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "gui/FormattedText.h"

namespace lurker::gui {

ChatView::ChatView(Kind kind, QWidget* parent) : QWidget(parent), m_kind(kind) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    if (kind == Kind::Channel) {
        m_topicBar = new QLabel(this);
        m_topicBar->setTextFormat(Qt::PlainText);
        m_topicBar->setWordWrap(false);
        m_topicBar->setContentsMargins(6, 3, 6, 3);
        m_topicBar->setTextInteractionFlags(Qt::TextSelectableByMouse);
        m_topicBar->setStyleSheet(QStringLiteral("QLabel { background: palette(alternate-base); }"));
        outer->addWidget(m_topicBar);
    }

    m_messages = new QTextBrowser(this);
    m_messages->setOpenExternalLinks(true);
    m_messages->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    if (kind == Kind::Channel) {
        auto* split = new QSplitter(Qt::Horizontal, this);
        split->addWidget(m_messages);
        m_nicklist = new QListWidget(split);
        m_nicklist->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        m_nicklist->setMaximumWidth(220);
        m_nicklist->setUniformItemSizes(true);
        split->addWidget(m_nicklist);
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 0);
        split->setSizes({560, 160});
        outer->addWidget(split, 1);
        connect(m_nicklist, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            QString nick = item->text();
            // strip a leading prefix sigil for the emitted nick
            if (!nick.isEmpty() && QStringLiteral("~&@%+").contains(nick.front())) nick.remove(0, 1);
            emit nickActivated(nick);
        });
    } else {
        outer->addWidget(m_messages, 1);
    }

    m_input = new QLineEdit(this);
    m_input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_input->setClearButtonEnabled(false);
    m_input->setPlaceholderText(kind == Kind::Status ? tr("Type a command, e.g. /join #channel")
                                                     : tr("Message, or /command"));
    outer->addWidget(m_input);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_input->text();
        if (text.isEmpty()) return;
        m_input->clear();
        emit lineEntered(text);
    });
}

void ChatView::appendBlock() {
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    if (!m_messages->document()->isEmpty()) cursor.insertBlock();
}

void ChatView::addMessage(const QString& timestamp, const QString& nick, const QByteArray& utf8Text, bool action,
                          bool selfEcho, bool highlight) {
    const bool atBottom = m_messages->verticalScrollBar()->value() >= m_messages->verticalScrollBar()->maximum() - 4;
    appendBlock();
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    const QPalette pal = m_messages->palette();

    QTextCharFormat meta;
    meta.setForeground(pal.color(QPalette::Disabled, QPalette::Text));
    if (!timestamp.isEmpty()) cursor.insertText(QStringLiteral("%1 ").arg(timestamp), meta);

    QTextCharFormat nickFmt;
    nickFmt.setForeground(highlight ? QColor(0xE5, 0xB5, 0x67)
                                    : (selfEcho ? pal.color(QPalette::Disabled, QPalette::Text)
                                                : pal.color(QPalette::Link)));
    if (action) {
        cursor.insertText(QStringLiteral("* %1 ").arg(nick), nickFmt);
    } else if (!nick.isEmpty()) {
        cursor.insertText(QStringLiteral("<%1> ").arg(nick), nickFmt);
    }

    insertFormatted(cursor, utf8Text, {pal.color(QPalette::Text), pal.color(QPalette::Base)});
    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
}

void ChatView::addSystemLine(const QByteArray& utf8Text) {
    const bool atBottom = m_messages->verticalScrollBar()->value() >= m_messages->verticalScrollBar()->maximum() - 4;
    appendBlock();
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat fmt;
    fmt.setForeground(m_messages->palette().color(QPalette::Disabled, QPalette::Text));
    cursor.insertText(QStringLiteral("— "), fmt);
    cursor.insertText(QString::fromUtf8(utf8Text), fmt);
    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
}

void ChatView::setTopic(const QByteArray& utf8Topic) {
    if (!m_topicBar) return;
    const QString topic = QString::fromUtf8(utf8Topic);
    m_topicBar->setText(topic.isEmpty() ? tr("(no topic set)") : stripFormattingQt(utf8Topic));
    m_topicBar->setToolTip(topic);
}

void ChatView::setMembers(const QStringList& sigils, const QStringList& nicks) {
    if (!m_nicklist) return;
    m_nicklist->clear();
    for (int i = 0; i < nicks.size(); ++i) {
        const QString sigil = i < sigils.size() ? sigils[i] : QString();
        m_nicklist->addItem(sigil + nicks[i]);
    }
}

int ChatView::memberCount() const { return m_nicklist ? m_nicklist->count() : 0; }

void ChatView::focusInput() { m_input->setFocus(); }

}  // namespace lurker::gui
