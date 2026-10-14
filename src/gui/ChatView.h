// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// ChatView is one conversation window's contents: an optional topic bar, the
// scrolling message area, an optional nicklist, and the input box. One widget
// serves status, channel and query buffers; the kind decides which parts show.
#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
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

    void setTopic(const QByteArray& utf8Topic);
    // Members as display strings already sorted by rank then name; `sigils`
    // holds the leading prefix glyph ("@", "+", "") parallel to `nicks`.
    void setMembers(const QStringList& sigils, const QStringList& nicks);
    int memberCount() const;

    void focusInput();

signals:
    void lineEntered(const QString& text);
    void nickActivated(const QString& nick);  // double-click in the nicklist

private:
    void appendBlock();

    Kind m_kind;
    QLabel* m_topicBar = nullptr;
    QTextBrowser* m_messages = nullptr;
    QListWidget* m_nicklist = nullptr;
    QLineEdit* m_input = nullptr;
};

}  // namespace lurker::gui
