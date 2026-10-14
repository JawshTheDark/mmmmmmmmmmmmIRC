// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ChatView is one conversation window's contents: an optional topic bar, the
// scrolling message area, an optional nicklist, and the input box. One widget
// serves status, channel and query buffers; the kind decides which parts show.
#pragma once

#include <QStringList>
#include <QWidget>
#include <functional>

class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QObject;
class QTextBrowser;

namespace lurker::gui {

class ChatView : public QWidget {
    Q_OBJECT
public:
    enum class Kind { Status, Channel, Query };

    explicit ChatView(Kind kind, QWidget* parent = nullptr);

    Kind kind() const { return m_kind; }

    // One rendered line. `nick` empty => a system/server line (dimmer, no <>).
    // `utf8Text` may carry IRC formatting codes. `selfEcho` tints your own lines.
    void addMessage(const QString& timestamp, const QString& nick, const QByteArray& utf8Text,
                    bool action = false, bool selfEcho = false, bool highlight = false);
    void addSystemLine(const QByteArray& utf8Text);

    void clearMessages();
    void setTopic(const QByteArray& utf8Topic);
    // Members as display strings already sorted by rank then name; `sigils`
    // holds the leading prefix glyph ("@", "+", "") parallel to `nicks`.
    void setMembers(const QStringList& sigils, const QStringList& nicks);
    int memberCount() const;

    void focusInput();

    // Re-read Options ▸ Display (font, scrollback) and apply to this view, so
    // theming changes take effect without reopening the window.
    void applyDisplaySettings();

    // Supplies nick candidates (no sigils) for tab-completion; set by the owner.
    void setNickProvider(std::function<QStringList()> provider) { m_nickProvider = std::move(provider); }

signals:
    void lineEntered(const QString& text);
    void nickActivated(const QString& nick);  // double-click in the nicklist

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void appendBlock();
    void recallHistory(int direction);  // -1 older, +1 newer
    void doTabCompletion();

    Kind m_kind;
    QLabel* m_topicBar = nullptr;
    QTextBrowser* m_messages = nullptr;
    QListWidget* m_nicklist = nullptr;
    QLineEdit* m_input = nullptr;

    std::function<QStringList()> m_nickProvider;

    QStringList m_history;          // sent lines, oldest first
    int m_historyIndex = -1;       // -1 = not recalling; else index into m_history
    QString m_historyDraft;        // what was typed before recall began

    QStringList m_compTexts;        // candidate replacement lines, current cycle
    QList<int> m_compCursors;       // caret positions parallel to m_compTexts
    int m_compIndex = -1;
    QString m_compApplied;          // the last completion we put in the box
};

}  // namespace lurker::gui
