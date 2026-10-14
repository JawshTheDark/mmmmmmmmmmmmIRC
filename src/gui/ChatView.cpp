// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ChatView.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "core/CommandParser.h"
#include "core/Completion.h"
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
    m_messages->document()->setMaximumBlockCount(5000);  // scrollback cap

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
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_input->text();
        if (text.isEmpty()) return;
        if (m_history.isEmpty() || m_history.last() != text) m_history.append(text);
        m_historyIndex = -1;
        m_compIndex = -1;
        m_input->clear();
        emit lineEntered(text);
    });
}

bool ChatView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
            case Qt::Key_Tab:
                doTabCompletion();
                return true;
            case Qt::Key_Up:
                recallHistory(-1);
                return true;
            case Qt::Key_Down:
                recallHistory(+1);
                return true;
            default:
                m_compIndex = -1;  // any other key ends a completion cycle
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatView::recallHistory(int direction) {
    if (m_history.isEmpty()) return;
    if (m_historyIndex < 0) {
        if (direction > 0) return;  // Down with no active recall does nothing
        m_historyDraft = m_input->text();
        m_historyIndex = m_history.size();
    }
    m_historyIndex += direction;
    if (m_historyIndex >= m_history.size()) {
        m_historyIndex = -1;
        m_input->setText(m_historyDraft);
    } else {
        if (m_historyIndex < 0) m_historyIndex = 0;
        m_input->setText(m_history.at(m_historyIndex));
    }
    m_input->end(false);
}

void ChatView::doTabCompletion() {
    // Continue an active cycle when the box still holds our last completion.
    if (m_compIndex >= 0 && !m_compTexts.isEmpty() && m_input->text() == m_compApplied) {
        m_compIndex = (m_compIndex + 1) % m_compTexts.size();
    } else {
        const std::string text = m_input->text().toStdString();
        const auto cursor = static_cast<std::size_t>(m_input->cursorPosition());
        const lurker::WordSpan word = lurker::wordBeforeCursor(text, cursor);
        const std::string prefix = text.substr(word.start, word.length());
        if (prefix.empty()) return;

        std::vector<std::string> pool;
        std::string startSuffix = ": ", midSuffix = " ";
        if (prefix.front() == '/') {
            pool = lurker::knownCommands();
            startSuffix = midSuffix = " ";  // commands never take the "nick: " form
        } else if (m_nickProvider) {
            for (const QString& n : m_nickProvider()) pool.push_back(n.toStdString());
        }
        const auto completions = lurker::buildCompletions(text, cursor, pool, startSuffix, midSuffix);
        m_compTexts.clear();
        m_compCursors.clear();
        for (const auto& c : completions) {
            m_compTexts << QString::fromStdString(c.text);
            m_compCursors << static_cast<int>(c.cursor);
        }
        if (m_compTexts.isEmpty()) return;
        m_compIndex = 0;
    }
    m_input->setText(m_compTexts.at(m_compIndex));
    m_input->setCursorPosition(m_compCursors.at(m_compIndex));
    m_compApplied = m_input->text();
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
