// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ChatView.h"

#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontDatabase>
#include <QFontDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QRegularExpression>
#include <QSettings>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>

#include "core/CommandParser.h"
#include "core/Completion.h"
#include "gui/FormattedText.h"
#include "gui/MtsTheme.h"
#include "gui/PreviewService.h"

namespace lurker::gui {

namespace {
// Reads a setting, preferring a per-channel override under `scope` when one is
// set, otherwise the global key. (Per-channel theming, Phase 3.6.)
QVariant scoped(const QString& scope, const QString& key, const QString& globalKey, const QVariant& def) {
    QSettings s;
    if (!scope.isEmpty()) {
        const QString k = scope + QLatin1Char('/') + key;
        if (s.contains(k)) return s.value(k);
    }
    return s.value(globalKey, def);
}

// The chat font comes from Options ▸ Display (family + size), with an optional
// per-channel override; it falls back to the platform's fixed-width font.
QFont configuredChatFont(const QString& scope = QString()) {
    const QString family = scoped(scope, QStringLiteral("fontFamily"), QStringLiteral("display/fontFamily"),
                                  QString()).toString();
    const int size = scoped(scope, QStringLiteral("fontSize"), QStringLiteral("display/fontSize"), 10).toInt();
    QFont f = family.isEmpty() ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QFont(family);
    if (size > 0) f.setPointSize(size);
    return f;
}

// A user-configurable colour for an event element (timestamp, join, notice, …):
// `colors/event/<name>`, with a per-channel override, falling back to `def` when
// unset. Lets users colour every kind of line to taste.
QColor eventColor(const QString& scope, const QString& name, const QColor& def) {
    const QString key = QStringLiteral("colors/event/") + name;
    const QString v = scoped(scope, key, key, QString()).toString();
    const QColor c(v);
    return (v.isEmpty() || !c.isValid()) ? def : c;
}

// A distinct default colour per system-event kind, so the buffer reads as
// colourful out of the box instead of one flat grey (#22). Muted, theme-aware
// hues; a user's colors/event/<name> setting still overrides these.
QColor defaultEventColor(const QString& name, const QPalette& pal) {
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    const int sat = dark ? 115 : 160, val = dark ? 205 : 150;
    auto hue = [&](int h) { return QColor::fromHsv(h, sat, val); };
    if (name == QLatin1String("join")) return hue(135);   // green
    if (name == QLatin1String("part")) return hue(30);    // amber
    if (name == QLatin1String("quit")) return hue(5);     // red
    if (name == QLatin1String("nick")) return hue(280);   // purple
    if (name == QLatin1String("mode")) return hue(205);   // blue
    if (name == QLatin1String("kick")) return hue(350);   // pink-red
    if (name == QLatin1String("topic")) return hue(175);  // teal
    return pal.color(QPalette::Disabled, QPalette::Text);  // generic: dim
}

// A stable per-nick colour (hash → hue), tuned for readability on the current
// theme's message background. mIRC colours nicks this way.
QColor hashNickColor(const QString& nick, const QPalette& pal) {
    const uint h = qHash(nick.toLower());
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    return QColor::fromHsv(static_cast<int>(h % 360), dark ? 150 : 200, dark ? 230 : 160);
}
}  // namespace

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
    m_messages->setFont(configuredChatFont());
    m_messages->document()->setMaximumBlockCount(
        QSettings().value(QStringLiteral("display/scrollback"), 5000).toInt());  // scrollback cap
    // Right-click menu on the chat area: standard Copy/Select-All plus any
    // script-defined `menu channel/query/status` items.
    m_messages->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_messages, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = m_messages->createStandardContextMenu(m_messages->mapToGlobal(pos));
        menu->addSeparator();
        menu->addAction(tr("Font…"), this, [this] { chooseFont(); });  // this window's font
        if (m_windowPopupProvider) m_windowPopupProvider(menu);
        menu->exec(m_messages->mapToGlobal(pos));
        menu->deleteLater();
    });

    if (kind == Kind::Channel) {
        auto* split = new QSplitter(Qt::Horizontal, this);
        m_messages->setMinimumWidth(120);  // chat pane always stays usable
        split->addWidget(m_messages);
        m_nicklist = new QListWidget(split);
        m_nicklist->setFont(configuredChatFont());
        m_nicklist->setMinimumWidth(90);   // nicklist stays readable, independent of content
        m_nicklist->setMaximumWidth(220);
        m_nicklist->setUniformItemSizes(true);
        split->addWidget(m_nicklist);
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 0);
        // Neither pane may be dragged/squeezed shut; both keep their minimum width.
        split->setChildrenCollapsible(false);
        split->setSizes({560, 160});
        outer->addWidget(split, 1);
        connect(m_nicklist, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
            // Drop any theme colour codes, then the leading prefix sigil, so the
            // query opens to the real nick (not "7@WAR-GEN").
            QString nick = stripFormattingQt(item->text().toUtf8());
            if (!nick.isEmpty() && QStringLiteral("~&@%+").contains(nick.front())) nick.remove(0, 1);
            emit nickActivated(nick);
        });
        // mIRC-style right-click menu on the nicklist.
        m_nicklist->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_nicklist, &QListWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
            QListWidgetItem* item = m_nicklist->itemAt(pos);
            if (!item) return;
            QString nick = stripFormattingQt(item->text().toUtf8());  // drop theme colour codes
            if (!nick.isEmpty() && QStringLiteral("~&@%+").contains(nick.front())) nick.remove(0, 1);
            QMenu menu(this);
            menu.addAction(nick)->setEnabled(false);
            menu.addSeparator();
            // Prefer the editable Popups `menu nicklist` (seeded with the defaults,
            // merged with any script's). Only if none is defined do we fall back to
            // the built-in menu, so there's never an empty right-click.
            const int base = menu.actions().size();
            if (m_nickPopupProvider) m_nickPopupProvider(&menu, nick);
            if (menu.actions().size() == base) {
                auto act = [&](QMenu* m, const QString& label, const QString& action) {
                    m->addAction(label, this, [this, nick, action] { emit nickContextAction(nick, action); });
                };
                act(&menu, tr("Query"), QStringLiteral("query"));
                act(&menu, tr("Whois"), QStringLiteral("whois"));
                act(&menu, tr("Send file…"), QStringLiteral("sendfile"));
                act(&menu, tr("Slap"), QStringLiteral("slap"));
                menu.addSeparator();
                QMenu* control = menu.addMenu(tr("Control"));
                act(control, tr("Op (+o)"), QStringLiteral("op"));
                act(control, tr("Deop (-o)"), QStringLiteral("deop"));
                act(control, tr("Voice (+v)"), QStringLiteral("voice"));
                act(control, tr("Devoice (-v)"), QStringLiteral("devoice"));
                control->addSeparator();
                act(control, tr("Kick"), QStringLiteral("kick"));
                act(control, tr("Ban"), QStringLiteral("ban"));
                act(control, tr("Kick + Ban"), QStringLiteral("kickban"));
                QMenu* ctcp = menu.addMenu(tr("CTCP"));
                act(ctcp, tr("Version"), QStringLiteral("ctcp-version"));
                act(ctcp, tr("Ping"), QStringLiteral("ctcp-ping"));
                act(ctcp, tr("Time"), QStringLiteral("ctcp-time"));
                act(ctcp, tr("Client Info"), QStringLiteral("ctcp-clientinfo"));
            }
            menu.exec(m_nicklist->mapToGlobal(pos));
        });
    } else {
        outer->addWidget(m_messages, 1);
    }

    m_input = new QLineEdit(this);
    m_input->setFont(configuredChatFont());
    m_input->setClearButtonEnabled(false);
    m_input->setPlaceholderText(kind == Kind::Status ? tr("Type a command, e.g. /join #channel")
                                                     : tr("Message, or /command"));
    outer->addWidget(m_input);
    m_input->installEventFilter(this);
    // Drag a file/image onto the window (or the editbox), or paste one, to share
    // it via Lurker's upload. Accept drops on the view, and filter the editbox so
    // a file drop there uploads rather than pasting its path as text.
    setAcceptDrops(true);
    m_messages->installEventFilter(this);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_input->text();
        if (text.isEmpty()) return;
        if (m_history.isEmpty() || m_history.last() != text) m_history.append(text);
        m_historyIndex = -1;
        m_compIndex = -1;
        m_input->clear();
        emit lineEntered(text);
    });

    applyDisplaySettings();  // apply any configured background on creation
}

void ChatView::setSettingsScope(const QString& scope) {
    m_settingsScope = scope;
    applyDisplaySettings();  // re-apply with any per-channel overrides
}

void ChatView::setNicklistVisible(int mode) {
    if (!m_nicklist) return;  // status/query windows have no nicklist
    const bool show = (mode < 0) ? !m_nicklist->isVisible() : (mode != 0);
    m_nicklist->setVisible(show);
}

void ChatView::applyDisplaySettings() {
    const QFont f = configuredChatFont(m_settingsScope);
    m_messages->setFont(f);
    if (m_nicklist) m_nicklist->setFont(f);
    m_input->setFont(f);
    m_messages->document()->setMaximumBlockCount(
        scoped(m_settingsScope, QStringLiteral("scrollback"), QStringLiteral("display/scrollback"), 5000).toInt());

    // Window background: a solid colour and/or an image, global or per-channel.
    // A widget stylesheet overrides the app skin, so an empty setting clears it.
    const QString bgColor =
        scoped(m_settingsScope, QStringLiteral("bgColor"), QStringLiteral("display/bgColor"), QString()).toString();
    const QString bgImage =
        scoped(m_settingsScope, QStringLiteral("bgImage"), QStringLiteral("display/bgImage"), QString()).toString();
    QString css;
    if (!bgColor.isEmpty() || !bgImage.isEmpty()) {
        css = QStringLiteral("QTextBrowser{");
        if (!bgColor.isEmpty()) css += QStringLiteral("background-color:%1;").arg(bgColor);
        if (!bgImage.isEmpty()) {
            css += QStringLiteral("background-image:url(\"%1\");background-position:center;"
                                  "background-attachment:fixed;")
                       .arg(bgImage);
        }
        css += QStringLiteral("}");
    }
    m_messages->setStyleSheet(css);

    // Nicklist background (colour and/or image) and text colour, global or per-channel.
    if (m_nicklist) {
        const QString nbColor =
            scoped(m_settingsScope, QStringLiteral("nicklistBgColor"), QStringLiteral("display/nicklistBgColor"),
                   QString()).toString();
        const QString nbImage =
            scoped(m_settingsScope, QStringLiteral("nicklistBgImage"), QStringLiteral("display/nicklistBgImage"),
                   QString()).toString();
        const QString ntColor =
            scoped(m_settingsScope, QStringLiteral("nicklistColor"), QStringLiteral("display/nicklistColor"),
                   QString()).toString();
        QString ncss;
        if (!nbColor.isEmpty() || !nbImage.isEmpty() || !ntColor.isEmpty()) {
            ncss = QStringLiteral("QListWidget{");
            if (!nbColor.isEmpty()) ncss += QStringLiteral("background-color:%1;").arg(nbColor);
            if (!nbImage.isEmpty()) ncss += QStringLiteral("background-image:url(\"%1\");").arg(nbImage);
            if (!ntColor.isEmpty()) ncss += QStringLiteral("color:%1;").arg(ntColor);
            ncss += QStringLiteral("}");
        }
        m_nicklist->setStyleSheet(ncss);
    }
}

bool ChatView::eventFilter(QObject* watched, QEvent* event) {
    // A file/image dropped on the editbox or the message area shares it rather
    // than letting the child widget paste the path or ignore the drop.
    if (watched == m_input || watched == m_messages) {
        if (event->type() == QEvent::DragEnter) {
            auto* de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData() && (de->mimeData()->hasUrls() || de->mimeData()->hasImage())) {
                de->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto* de = static_cast<QDropEvent*>(event);
            if (shareMimeData(de->mimeData())) {
                de->acceptProposedAction();
                return true;
            }
        }
    }
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        // Paste (Ctrl+V / Shift+Insert) of a bare image or local files → share it,
        // instead of pasting a path as text. Ordinary text pastes fall through.
        const bool paste = ((key->modifiers() & Qt::ControlModifier) && key->key() == Qt::Key_V) ||
                           ((key->modifiers() & Qt::ShiftModifier) && key->key() == Qt::Key_Insert);
        if (paste) {
            const QMimeData* cb = QGuiApplication::clipboard()->mimeData();
            bool localFiles = false;
            if (cb && cb->hasUrls())
                for (const QUrl& u : cb->urls())
                    if (u.isLocalFile()) {
                        localFiles = true;
                        break;
                    }
            const bool bareImage = cb && cb->hasImage() && !cb->hasText() && !cb->hasUrls();
            if ((localFiles || bareImage) && shareMimeData(cb)) return true;
        }
        // mIRC editbox formatting: Ctrl+B/U/I/R/O/K insert the control codes so
        // formatted text can be composed (FormattedText renders them on send).
        if ((key->modifiers() & Qt::ControlModifier) && !(key->modifiers() & Qt::AltModifier)) {
            QChar code;
            switch (key->key()) {
                case Qt::Key_B: code = QChar(0x02); break;  // bold
                case Qt::Key_K: code = QChar(0x03); break;  // colour (fg[,bg] follows)
                case Qt::Key_O: code = QChar(0x0F); break;  // reset
                case Qt::Key_R: code = QChar(0x16); break;  // reverse
                case Qt::Key_I: code = QChar(0x1D); break;  // italic
                case Qt::Key_U: code = QChar(0x1F); break;  // underline
                default: break;
            }
            if (!code.isNull()) {
                m_input->insert(QString(code));
                return true;
            }
        }
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

void ChatView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData() && (event->mimeData()->hasUrls() || event->mimeData()->hasImage()))
        event->acceptProposedAction();
}

void ChatView::dropEvent(QDropEvent* event) {
    if (shareMimeData(event->mimeData())) event->acceptProposedAction();
}

bool ChatView::shareMimeData(const QMimeData* mime) {
    if (!mime) return false;
    if (mime->hasUrls()) {  // files dragged from a file manager, or copied to the clipboard
        QStringList paths;
        for (const QUrl& u : mime->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (!paths.isEmpty()) {
            emit shareFiles(paths);
            return true;
        }
    }
    if (mime->hasImage()) {  // a screenshot or a copied bitmap with no backing file
        const QImage img = qvariant_cast<QImage>(mime->imageData());
        if (!img.isNull()) {
            emit shareImage(img);
            return true;
        }
    }
    return false;
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
                          bool selfEcho, bool highlight, qlonglong msgId, const QString& replyNick,
                          const QString& replyText) {
    const bool atBottom = m_messages->verticalScrollBar()->value() >= m_messages->verticalScrollBar()->maximum() - 4;
    appendBlock();
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    const QPalette pal = m_messages->palette();

    // A reply: draw a dim quote of the answered line above this one.
    if (!replyNick.isEmpty()) {
        QTextCharFormat quote;
        quote.setForeground(pal.color(QPalette::Disabled, QPalette::Text));
        QString t = replyText;
        if (t.size() > 80) t = t.left(79) + QChar(0x2026);  // clip with an ellipsis
        cursor.insertText(QStringLiteral("↱ %1: %2\n").arg(replyNick, t), quote);
    }

    QTextCharFormat meta;
    meta.setForeground(eventColor(m_settingsScope, QStringLiteral("timestamp"),
                                  pal.color(QPalette::Disabled, QPalette::Text)));
    if (!timestamp.isEmpty()) cursor.insertText(QStringLiteral("%1 ").arg(timestamp), meta);

    const bool colorNicks = QSettings().value(QStringLiteral("display/colorNicks"), true).toBool();
    QColor nickColor;
    if (highlight) {
        nickColor = QColor(0xE5, 0xB5, 0x67);
    } else if (selfEcho) {
        nickColor = pal.color(QPalette::Disabled, QPalette::Text);
    } else if (colorNicks && !nick.isEmpty()) {
        nickColor = hashNickColor(nick, pal);
    } else {
        nickColor = pal.color(QPalette::Link);
    }
    QTextCharFormat nickFmt;
    nickFmt.setForeground(nickColor);
    nickFmt.setFontWeight(QFont::Bold);
    if (action) {
        cursor.insertText(QStringLiteral("* %1 ").arg(nick), nickFmt);
    } else if (!nick.isEmpty()) {
        cursor.insertText(QStringLiteral("<%1> ").arg(nick), nickFmt);
    }

    if (QSettings().value(QStringLiteral("display/stripColors"), false).toBool()) {
        QTextCharFormat plain;
        plain.setForeground(eventColor(m_settingsScope, QStringLiteral("message"), pal.color(QPalette::Text)));
        cursor.insertText(stripFormattingQt(utf8Text), plain);
    } else {
        insertFormatted(cursor, utf8Text,
                        {eventColor(m_settingsScope, QStringLiteral("message"), pal.color(QPalette::Text)),
                         pal.color(QPalette::Base)});
    }
    // Remember where this message ends so reactions arriving later can be drawn
    // under it. keepPositionOnInsert pins the anchor as more lines append below.
    if (msgId != 0) {
        ReactSlot slot;
        slot.anchor = QTextCursor(m_messages->document());
        slot.anchor.setPosition(cursor.position());
        slot.anchor.setKeepPositionOnInsert(true);
        m_reactSlots.insert(msgId, slot);
    }

    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
    requestPreviews(stripFormattingQt(utf8Text));
}

void ChatView::applyReaction(qlonglong msgId, const QString& nick, const QString& value, bool selfReact,
                             bool remove) {
    auto it = m_reactSlots.find(msgId);
    if (it == m_reactSlots.end() || value.isEmpty()) return;  // message not shown here
    ReactSlot& s = it.value();
    if (remove) {
        auto vi = s.nicks.find(value);
        if (vi != s.nicks.end()) {
            vi.value().remove(nick);
            if (vi.value().isEmpty()) s.nicks.erase(vi);
        }
        if (selfReact) s.selfValues.remove(value);
    } else {
        s.nicks[value].insert(nick);
        if (selfReact) s.selfValues.insert(value);
    }
    renderChips(msgId);
}

void ChatView::renderChips(qlonglong msgId) {
    auto it = m_reactSlots.find(msgId);
    if (it == m_reactSlots.end()) return;
    ReactSlot& s = it.value();

    QString chips;
    for (auto vi = s.nicks.constBegin(); vi != s.nicks.constEnd(); ++vi) {
        if (vi.value().isEmpty()) continue;
        if (!chips.isEmpty()) chips += QStringLiteral("  ");
        chips += vi.key() + QLatin1Char(' ') + QString::number(vi.value().size());
        if (s.selfValues.contains(vi.key())) chips += QChar(0x2713);  // ✓ marks our own reaction
    }
    const QString run = chips.isEmpty() ? QString() : (QStringLiteral("\n   ") + chips);

    const bool atBottom =
        m_messages->verticalScrollBar()->value() >= m_messages->verticalScrollBar()->maximum() - 4;
    QTextCursor c(m_messages->document());
    c.setPosition(s.anchor.position());
    if (s.chipLen > 0) {  // delete the previously-drawn chip run
        c.setPosition(s.anchor.position() + s.chipLen, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setPosition(s.anchor.position());
    }
    if (!run.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setForeground(m_messages->palette().color(QPalette::Disabled, QPalette::Text));
        c.insertText(run, fmt);
    }
    s.chipLen = run.size();
    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
}

void ChatView::addNotice(const QString& timestamp, const QString& from, const QByteArray& utf8Text, bool server) {
    const bool atBottom = m_messages->verticalScrollBar()->value() >= m_messages->verticalScrollBar()->maximum() - 4;
    appendBlock();
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    const QPalette pal = m_messages->palette();

    QTextCharFormat meta;
    meta.setForeground(eventColor(m_settingsScope, QStringLiteral("timestamp"),
                                  pal.color(QPalette::Disabled, QPalette::Text)));
    if (!timestamp.isEmpty()) cursor.insertText(QStringLiteral("%1 ").arg(timestamp), meta);

    // Server notices get a distinct colour and an "snotice" tag so they don't
    // read as a message from a user; user/service notices use the link colour.
    const bool dark = pal.color(QPalette::Base).lightness() < 128;
    const QColor snoticeDef = dark ? QColor(0x7F, 0xC9, 0xC9) : QColor(0x1F, 0x7A, 0x7A);
    QTextCharFormat fromFmt;
    fromFmt.setFontWeight(QFont::Bold);
    fromFmt.setForeground(server ? eventColor(m_settingsScope, QStringLiteral("snotice"), snoticeDef)
                                 : eventColor(m_settingsScope, QStringLiteral("notice"), pal.color(QPalette::Link)));
    const QString label = server ? QStringLiteral("-%1- (snotice) ").arg(from) : QStringLiteral("-%1- ").arg(from);
    cursor.insertText(label, fromFmt);

    if (QSettings().value(QStringLiteral("display/stripColors"), false).toBool()) {
        QTextCharFormat plain;
        plain.setForeground(eventColor(m_settingsScope, QStringLiteral("message"), pal.color(QPalette::Text)));
        cursor.insertText(stripFormattingQt(utf8Text), plain);
    } else {
        insertFormatted(cursor, utf8Text,
                        {eventColor(m_settingsScope, QStringLiteral("message"), pal.color(QPalette::Text)),
                         pal.color(QPalette::Base)});
    }
    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
    requestPreviews(stripFormattingQt(utf8Text));
}

void ChatView::addSystemLine(const QByteArray& utf8Text, SystemKind kind) {
    const bool atBottom = m_messages->verticalScrollBar()->value() >= m_messages->verticalScrollBar()->maximum() - 4;
    appendBlock();
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    const QColor dim = m_messages->palette().color(QPalette::Disabled, QPalette::Text);
    QString name;
    switch (kind) {
        case SystemKind::Join: name = QStringLiteral("join"); break;
        case SystemKind::Part: name = QStringLiteral("part"); break;
        case SystemKind::Quit: name = QStringLiteral("quit"); break;
        case SystemKind::Nick: name = QStringLiteral("nick"); break;
        case SystemKind::Mode: name = QStringLiteral("mode"); break;
        case SystemKind::Kick: name = QStringLiteral("kick"); break;
        case SystemKind::Topic: name = QStringLiteral("topic"); break;
        case SystemKind::Generic: break;
    }
    QTextCharFormat fmt;
    fmt.setForeground(name.isEmpty()
                          ? dim
                          : eventColor(m_settingsScope, name,
                                       defaultEventColor(name, m_messages->palette())));
    cursor.insertText(QStringLiteral("— "), fmt);
    cursor.insertText(QString::fromUtf8(utf8Text), fmt);
    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
}

void ChatView::addThemedLine(const QString& timestamp, const QByteArray& utf8) {
    const bool atBottom = m_messages->verticalScrollBar()->value() >=
                          m_messages->verticalScrollBar()->maximum() - 4;
    appendBlock();
    QTextCursor cursor(m_messages->document());
    cursor.movePosition(QTextCursor::End);
    const QPalette pal = m_messages->palette();
    if (!timestamp.isEmpty()) {
        QTextCharFormat meta;
        meta.setForeground(eventColor(m_settingsScope, QStringLiteral("timestamp"),
                                      pal.color(QPalette::Disabled, QPalette::Text)));
        cursor.insertText(QStringLiteral("%1 ").arg(timestamp), meta);
    }
    insertFormatted(cursor, utf8,
                    {eventColor(m_settingsScope, QStringLiteral("message"), pal.color(QPalette::Text)),
                     pal.color(QPalette::Base)});
    if (atBottom) m_messages->verticalScrollBar()->setValue(m_messages->verticalScrollBar()->maximum());
    // Inline link/media previews are attached to every render path (see addMessage
    // and addNotice); with an MTS theme active, message lines come through here, so
    // request them here too or themed buffers would silently lose their previews.
    requestPreviews(stripFormattingQt(utf8));
}

void ChatView::clearMessages() {
    m_messages->clear();
    m_reactSlots.clear();  // the anchors point into the now-cleared document
}

int ChatView::lineCount() const {
    const QTextDocument* doc = m_messages->document();
    return doc->isEmpty() ? 0 : doc->blockCount();
}

QString ChatView::lineAt(int oneBased) const {
    const QTextBlock b = m_messages->document()->findBlockByNumber(oneBased - 1);
    return b.isValid() ? b.text() : QString();
}

void ChatView::setTopic(const QByteArray& utf8Topic) {
    if (!m_topicBar) return;
    const QString topic = QString::fromUtf8(utf8Topic);
    m_topicBar->setText(topic.isEmpty() ? tr("(no topic set)") : stripFormattingQt(utf8Topic));
    m_topicBar->setToolTip(topic);
}

void ChatView::chooseFont() {
    bool ok = false;
    const QFont f = QFontDialog::getFont(&ok, m_messages->font(), this, tr("Window Font"));
    if (!ok) return;
    QSettings s;
    if (!m_settingsScope.isEmpty()) {  // per-channel/query override
        s.setValue(m_settingsScope + QStringLiteral("/fontFamily"), f.family());
        s.setValue(m_settingsScope + QStringLiteral("/fontSize"), f.pointSize());
    } else {  // status window → the global display font
        s.setValue(QStringLiteral("display/fontFamily"), f.family());
        s.setValue(QStringLiteral("display/fontSize"), f.pointSize());
    }
    applyDisplaySettings();
}

QString ChatView::currentTopic() const {
    // The topic bar keeps the raw topic as its tooltip (the label text is stripped).
    return m_topicBar ? m_topicBar->toolTip() : QString();
}

void ChatView::setMembers(const QStringList& sigils, const QStringList& nicks) {
    if (!m_nicklist) return;
    m_nicklist->clear();
    const lurker::gui::MtsTheme* theme = lurker::gui::activeMtsTheme();
    for (int i = 0; i < nicks.size(); ++i) {
        const QString sigil = i < sigils.size() ? sigils[i] : QString();
        QString glyph;
        QColor color;
        if (theme && theme->nicklistDecor(sigil, glyph, color)) {
            // The glyph may carry a colour code; strip it (lists can't render
            // codes) and apply the colour via the item's foreground instead.
            auto* item = new QListWidgetItem(stripFormattingQt(glyph.toUtf8()) + nicks[i], m_nicklist);
            if (color.isValid()) item->setForeground(color);
        } else {
            m_nicklist->addItem(sigil + nicks[i]);
        }
    }
}

int ChatView::memberCount() const { return m_nicklist ? m_nicklist->count() : 0; }

void ChatView::requestPreviews(const QString& plainText) {
    if (!m_previews || !m_previews->enabled()) return;
    static const QRegularExpression re(QStringLiteral(R"(https?://[^\s<>"')]+)"));
    auto it = re.globalMatch(plainText);
    int count = 0;
    while (it.hasNext() && count < 4) {
        QString url = it.next().captured(0);
        while (!url.isEmpty() && QStringLiteral(".,;:!?]}'\"").contains(url.back())) url.chop(1);
        if (url.isEmpty()) continue;
        ++count;
        // Anchor a cursor at the end of this message. keepPositionOnInsert keeps it
        // from drifting to the bottom as later lines (e.g. a backlog burst) append,
        // so the preview lands under the message it belongs to, not at the end.
        QTextCursor slot(m_messages->document());
        slot.movePosition(QTextCursor::End);
        slot.setKeepPositionOnInsert(true);
        m_previews->request(url, [this, slot](const PreviewDescriptor& d, const QImage& img) {
            insertPreview(slot, d, img);
        });
    }
}

void ChatView::insertPreview(QTextCursor slot, const PreviewDescriptor& d, const QImage& img) {
    if (!d.ok()) return;
    auto* sb = m_messages->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;

    const QPalette pal = m_messages->palette();
    const QColor base = pal.color(QPalette::Base);
    const bool dark = base.lightness() < 128;
    const QString cardBg = (dark ? base.lighter(135) : base.darker(106)).name();
    const QString border = pal.color(QPalette::Mid).name();
    const QString link = pal.color(QPalette::Link).name();
    const QString muted = pal.color(QPalette::Disabled, QPalette::Text).name();

    // Register the fetched pixels as a document resource and build a sized <img>.
    QString imgTag;
    if (!img.isNull()) {
        const QString resName = QStringLiteral("lurkerprev://%1").arg(qHash(d.imagePath()));
        m_messages->document()->addResource(QTextDocument::ImageResource, QUrl(resName), img);
        int w = img.width(), h = img.height();
        const int maxW = 400;
        if (w > maxW && w > 0) {
            h = h * maxW / w;
            w = maxW;
        }
        imgTag = QStringLiteral("<img src=\"%1\" width=\"%2\" height=\"%3\">").arg(resName).arg(w).arg(h);
    }

    const QString clickUrl = (d.kind == QLatin1String("video-embed") && !d.embedUrl.isEmpty()) ? d.embedUrl : d.url;
    QString html;
    if (d.kind == QLatin1String("image") && !imgTag.isEmpty()) {
        // A direct image: show it inline, linking to the original.
        html = QStringLiteral("<a href=\"%1\">%2</a>").arg(d.url.toHtmlEscaped(), imgTag);
    } else {
        // A card: title, description, site, then the thumbnail/poster. Media kinds
        // (video/audio/embed) get a ▶ marker — clicking opens the source.
        const bool playable = d.kind == QLatin1String("video") || d.kind == QLatin1String("audio") ||
                              d.kind == QLatin1String("video-embed");
        const QString title = (d.title.isEmpty() ? d.url : d.title).toHtmlEscaped();
        QString body = QStringLiteral("<a href=\"%1\" style=\"color:%2;font-weight:bold;text-decoration:none;\">%3%4</a>")
                           .arg(clickUrl.toHtmlEscaped(), link, playable ? QStringLiteral("&#9654; ") : QString(), title);
        if (!d.description.isEmpty())
            body += QStringLiteral("<br><span style=\"color:%1;\">%2</span>").arg(muted, d.description.toHtmlEscaped());
        if (!d.siteName.isEmpty())
            body += QStringLiteral("<br><span style=\"color:%1;font-size:small;\">%2</span>")
                        .arg(muted, d.siteName.toHtmlEscaped());
        if (!imgTag.isEmpty()) body += QStringLiteral("<br>%1").arg(imgTag);
        html = QStringLiteral("<table width=\"420\" cellspacing=\"0\" cellpadding=\"6\" "
                              "style=\"background-color:%1;border:1px solid %2;\"><tr><td>%3</td></tr></table>")
                   .arg(cardBg, border, body);
    }

    // Insert via a fresh cursor at the anchored position (the slot itself keeps
    // its position on insert, so it can't be used to insert directly).
    QTextCursor c(m_messages->document());
    c.setPosition(slot.position());
    c.insertBlock();
    c.insertHtml(html);
    if (atBottom) sb->setValue(sb->maximum());
}

void ChatView::focusInput() { m_input->setFocus(); }

void ChatView::setInputText(const QString& text) {
    m_input->setText(text);
    m_input->setFocus();
    m_input->end(false);
}

}  // namespace lurker::gui
