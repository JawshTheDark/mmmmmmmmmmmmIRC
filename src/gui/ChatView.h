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
class QImage;
class QLabel;
class QLineEdit;
class QListWidget;
class QObject;
class QTextBrowser;
class QTextCursor;

namespace lurker::gui {

class PreviewService;
struct PreviewDescriptor;

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
    // A notice rendered mIRC-style as "-from- text". `server` marks a server
    // notice (SNOTICE) so it's coloured distinctly from a user/service notice.
    void addNotice(const QString& timestamp, const QString& from, const QByteArray& utf8Text, bool server);

    void clearMessages();
    int lineCount() const;          // $line(window,0)
    QString lineAt(int oneBased) const;  // $line(window,N)
    void setTopic(const QByteArray& utf8Topic);
    QString currentTopic() const;  // the raw topic text (for Channel Central)
    // Members as display strings already sorted by rank then name; `sigils`
    // holds the leading prefix glyph ("@", "+", "") parallel to `nicks`.
    void setMembers(const QStringList& sigils, const QStringList& nicks);
    int memberCount() const;

    void focusInput();
    void setInputText(const QString& text);  // pre-fill the composer (e.g. a shared file URL) and focus it
    void setNicklistVisible(int mode);  // -1 toggle, 0 hide, 1 show (channels only)

    // Re-read Options ▸ Display (font, scrollback) and apply to this view, so
    // theming changes take effect without reopening the window.
    void applyDisplaySettings();

    // Settings prefix for per-channel theme overrides (e.g. "chan/1/_lurker").
    // When set, applyDisplaySettings prefers keys under it over the globals.
    void setSettingsScope(const QString& scope);
    QString settingsScope() const { return m_settingsScope; }

    // Supplies nick candidates (no sigils) for tab-completion; set by the owner.
    void setNickProvider(std::function<QStringList()> provider) { m_nickProvider = std::move(provider); }

    // Link/media previews: when set (and enabled), URLs in incoming messages get
    // an inline image or card resolved through Lurker's previews proxy.
    void setPreviewService(PreviewService* svc) { m_previews = svc; }

    // Lets the owner append script-defined (menu nicklist) items to the nicklist
    // right-click menu; called with the menu and the clicked nick.
    void setNickPopupProvider(std::function<void(class QMenu*, const QString&)> provider) {
        m_nickPopupProvider = std::move(provider);
    }
    // Appends script-defined (menu channel/query/status) items to the message
    // area's right-click menu.
    void setWindowPopupProvider(std::function<void(class QMenu*)> provider) {
        m_windowPopupProvider = std::move(provider);
    }

signals:
    void lineEntered(const QString& text);
    void nickActivated(const QString& nick);  // double-click in the nicklist
    // Right-click nicklist action; `action` is one of query/whois/slap/op/deop/
    // voice/devoice/kick/ban/kickban/ctcp-<type>. The owner maps it to IRC.
    void nickContextAction(const QString& nick, const QString& action);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void appendBlock();
    void recallHistory(int direction);  // -1 older, +1 newer
    void doTabCompletion();
    // Find URLs in a just-rendered line and resolve previews for them, inserting
    // the result just under the line (the `slot` cursor tracks that position).
    void requestPreviews(const QString& plainText);
    void insertPreview(QTextCursor slot, const PreviewDescriptor& d, const QImage& img);

    Kind m_kind;
    QString m_settingsScope;  // per-channel theme override prefix, empty = globals only
    QLabel* m_topicBar = nullptr;
    QTextBrowser* m_messages = nullptr;
    QListWidget* m_nicklist = nullptr;
    QLineEdit* m_input = nullptr;

    std::function<QStringList()> m_nickProvider;
    std::function<void(class QMenu*, const QString&)> m_nickPopupProvider;
    std::function<void(class QMenu*)> m_windowPopupProvider;
    PreviewService* m_previews = nullptr;

    QStringList m_history;          // sent lines, oldest first
    int m_historyIndex = -1;       // -1 = not recalling; else index into m_history
    QString m_historyDraft;        // what was typed before recall began

    QStringList m_compTexts;        // candidate replacement lines, current cycle
    QList<int> m_compCursors;       // caret positions parallel to m_compTexts
    int m_compIndex = -1;
    QString m_compApplied;          // the last completion we put in the box
};

}  // namespace lurker::gui
