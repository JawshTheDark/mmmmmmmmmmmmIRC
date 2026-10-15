// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/OptionsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QInputDialog>
#include <QVector>
#include <memory>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "gui/Theme.h"

namespace lurker::gui {

namespace {
// mIRC's 16 default message colours (indices 0-15), drawn on the Colors page.
const char* kMircColors[16] = {"#FFFFFF", "#000000", "#00007F", "#009300", "#FF0000", "#7F0000",
                               "#9C009C", "#FC7F00", "#FFFF00", "#00FC00", "#009393", "#00FFFF",
                               "#0000FC", "#FF00FF", "#7F7F7F", "#D2D2D2"};
}  // namespace

OptionsDialog::OptionsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("mIRC Options"));
    resize(640, 480);

    auto* outer = new QVBoxLayout(this);
    auto* split = new QHBoxLayout;
    outer->addLayout(split, 1);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setMaximumWidth(190);
    m_tree->setMinimumWidth(160);
    split->addWidget(m_tree);

    m_stack = new QStackedWidget(this);
    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(m_stack);
    split->addWidget(frame, 1);

    // ---- Connect -------------------------------------------------------
    auto* connectTop = buildPage(tr("Connect"), tr("Your identity on IRC."), [&](QFormLayout* f) {
        f->addRow(tr("Nickname"), boundEdit(QStringLiteral("id/nick")));
        f->addRow(tr("Alternative"), boundEdit(QStringLiteral("id/altNick")));
        f->addRow(tr("Email"), boundEdit(QStringLiteral("id/email")));
        f->addRow(tr("Full name"), boundEdit(QStringLiteral("id/realname"), tr("Lurker Desktop")));
    });
    QTreeWidgetItem* connectNode = addCategory(nullptr, tr("Connect"), connectTop);

    addCategory(connectNode, tr("Servers"), buildPage(tr("Servers"), tr("Server to connect to."), [&](QFormLayout* f) {
                    f->addRow(tr("Server"), boundEdit(QStringLiteral("server/host"), QStringLiteral("chat.irc.so")));
                    f->addRow(tr("Port"), boundEdit(QStringLiteral("server/port"), QStringLiteral("1025")));
                    f->addRow(tr("Password"), [&] {
                        auto* e = boundEdit(QStringLiteral("server/password"));
                        e->setEchoMode(QLineEdit::Password);
                        return e;
                    }());
                    f->addRow(boundCheck(tr("Use SSL/TLS for this server"), QStringLiteral("server/ssl"), true));
                    f->addRow(boundCheck(tr("Connect through the Lurker bouncer"),
                                         QStringLiteral("server/bouncer"), true));
                }));
    addCategory(connectNode, tr("Options"),
                buildPage(tr("Connect Options"), tr("Reconnection and startup."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Reconnect on disconnection"), QStringLiteral("connect/reconnect"), true));
                    f->addRow(tr("Retry attempts"), boundSpin(QStringLiteral("connect/retries"), 0, 100, 10));
                    f->addRow(tr("Retry delay (s)"), boundSpin(QStringLiteral("connect/retryDelay"), 1, 600, 20));
                    f->addRow(boundCheck(tr("Rejoin channels on connect"), QStringLiteral("connect/rejoin"), true));
                    f->addRow(boundCheck(tr("Connect to server on startup"), QStringLiteral("connect/onStartup"),
                                         true));
                }));
    addCategory(connectNode, tr("Identd"),
                buildPage(tr("Identd"), tr("Ident server replies."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Enable identd server"), QStringLiteral("identd/enabled"), false));
                    f->addRow(tr("User ID"), boundEdit(QStringLiteral("identd/userId"), QStringLiteral("lurker")));
                    f->addRow(tr("System"), boundCombo(QStringLiteral("identd/system"),
                                                       {QStringLiteral("UNIX"), QStringLiteral("WIN32")},
                                                       QStringLiteral("UNIX")));
                }));
    addCategory(connectNode, tr("Local Info"),
                buildPage(tr("Local Info"), tr("How your address is determined."), [&](QFormLayout* f) {
                    f->addRow(tr("Local host"), boundEdit(QStringLiteral("local/host")));
                    f->addRow(tr("IP address"), boundEdit(QStringLiteral("local/ip")));
                    f->addRow(boundCheck(tr("Get IP/host automatically on connect"),
                                         QStringLiteral("local/autoLookup"), true));
                }));

    // ---- IRC -----------------------------------------------------------
    auto* ircTop = buildPage(tr("IRC"), tr("General IRC behaviour."), [&](QFormLayout* f) {
        f->addRow(boundCheck(tr("Rejoin channel when kicked"), QStringLiteral("irc/rejoinOnKick"), false));
        f->addRow(boundCheck(tr("Show mode changes"), QStringLiteral("irc/showModes"), true));
        f->addRow(boundCheck(tr("Show join/part/quit events"), QStringLiteral("irc/showJoins"), true));
        f->addRow(tr("Quit message"),
                  boundEdit(QStringLiteral("irc/quitMessage"), tr("Lurker Desktop")));
    });
    QTreeWidgetItem* ircNode = addCategory(nullptr, tr("IRC"), ircTop);
    addCategory(ircNode, tr("Messages"),
                buildPage(tr("Messages"), tr("Private messages and queries."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Open query window on private message"),
                                         QStringLiteral("msg/openQuery"), true));
                    f->addRow(boundCheck(tr("Show own messages in query"), QStringLiteral("msg/echoSelf"), true));
                    f->addRow(boundCheck(tr("Beep on private message"), QStringLiteral("msg/beep"), false));
                    f->addRow(boundCheck(tr("Whois on query open"), QStringLiteral("msg/whoisOnQuery"), false));
                    // mIRC-style message routing: where each category is shown.
                    f->addRow(tr("User notices"),
                              boundCombo(QStringLiteral("route/notice"),
                                         {tr("Active window"), tr("Status window"), tr("Separate query")},
                                         tr("Active window")));
                    f->addRow(tr("Whois / server replies"),
                              boundCombo(QStringLiteral("route/whois"),
                                         {tr("Active window"), tr("Status window")}, tr("Active window")));
                    f->addRow(tr("CTCP replies"),
                              boundCombo(QStringLiteral("route/ctcp"),
                                         {tr("Active window"), tr("Status window")}, tr("Active window")));
                }));
    addCategory(ircNode, tr("Highlight"),
                buildPage(tr("Highlight"), tr("Words that flag a line and notify you."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Enable highlighting"), QStringLiteral("highlight/enabled"), true));
                    f->addRow(tr("Highlight words"), boundEdit(QStringLiteral("highlight/words")));
                    f->addRow(boundCheck(tr("Highlight on my nick"), QStringLiteral("highlight/onNick"), true));
                    f->addRow(boundCheck(tr("Flash taskbar on highlight"), QStringLiteral("highlight/flash"), true));
                }));
    addCategory(ircNode, tr("Flood"),
                buildPage(tr("Flood"), tr("Flood protection for outgoing lines."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Enable flood protection"), QStringLiteral("flood/enabled"), true));
                    f->addRow(tr("Max lines"), boundSpin(QStringLiteral("flood/maxLines"), 1, 50, 5));
                    f->addRow(tr("Per seconds"), boundSpin(QStringLiteral("flood/perSeconds"), 1, 60, 3));
                }));

    // ---- DCC -----------------------------------------------------------
    auto* dccTop = buildPage(tr("DCC"), tr("File transfers and chats, proxied through Lurker."),
                             [&](QFormLayout* f) {
                                 f->addRow(boundCheck(tr("Proxy DCC through Lurker (recommended)"),
                                                      QStringLiteral("dcc/proxy"), true));
                                 f->addRow(boundCheck(tr("Enable DCC"), QStringLiteral("dcc/enabled"), true));
                             });
    QTreeWidgetItem* dccNode = addCategory(nullptr, tr("DCC"), dccTop);
    addCategory(dccNode, tr("Options"),
                buildPage(tr("DCC Options"), tr("How incoming transfers are handled."), [&](QFormLayout* f) {
                    f->addRow(tr("On Get request"),
                              boundCombo(QStringLiteral("dcc/onGet"),
                                         {tr("Ask"), tr("Auto-accept"), tr("Ignore")}, tr("Ask")));
                    f->addRow(tr("On Chat request"),
                              boundCombo(QStringLiteral("dcc/onChat"),
                                         {tr("Ask"), tr("Auto-accept"), tr("Ignore")}, tr("Ask")));
                    f->addRow(boundCheck(tr("Resume interrupted transfers"), QStringLiteral("dcc/resume"), true));
                }));
    addCategory(dccNode, tr("Folders"),
                buildPage(tr("DCC Folders"), tr("Where received files are saved."), [&](QFormLayout* f) {
                    f->addRow(tr("Download folder"),
                              boundEdit(QStringLiteral("dcc/downloadDir"),
                                        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)));
                    f->addRow(boundCheck(tr("Sort files into per-nick subfolders"),
                                         QStringLiteral("dcc/perNickFolders"), false));
                }));
    addCategory(dccNode, tr("Server"),
                buildPage(tr("DCC Server"), tr("Lurker relays transfers, so no local ports are opened."),
                          [&](QFormLayout* f) {
                              f->addRow(tr("Relay endpoint"),
                                        boundEdit(QStringLiteral("dcc/relay"), QStringLiteral("/api/dcc")));
                              f->addRow(tr("File host"),
                                        boundEdit(QStringLiteral("dcc/fileHost"), QStringLiteral("soju.im/FILEHOST")));
                          }));

    // ---- Display -------------------------------------------------------
    auto* displayTop = buildPage(tr("Display"), tr("How windows and text look."), [&](QFormLayout* f) {
        // Theme (applies immediately on save).
        auto* themeCombo = new QComboBox;
        for (const Theme& t : availableThemes()) themeCombo->addItem(t.name, t.id);
        m_bindings.push_back({themeCombo, QStringLiteral("theme"), QStringLiteral("default"), true});
        f->addRow(tr("Theme / skin"), themeCombo);
        // Font.
        auto* fontCombo = new QFontComboBox;
        fontCombo->setObjectName(QStringLiteral("fontFamily"));
        m_bindings.push_back({fontCombo, QStringLiteral("display/fontFamily"), QStringLiteral("Monospace"), false});
        f->addRow(tr("Font"), fontCombo);
        f->addRow(tr("Font size"), boundSpin(QStringLiteral("display/fontSize"), 6, 48, 10));
        f->addRow(boundCheck(tr("Show timestamps"), QStringLiteral("display/timestamps"), true));
        f->addRow(tr("Time format"), boundCombo(QStringLiteral("display/timeFormat"),
                                                {QStringLiteral("HH:mm"), QStringLiteral("HH:mm:ss"),
                                                 QStringLiteral("hh:mm ap"), QStringLiteral("[HH:mm]")},
                                                QStringLiteral("HH:mm")));
        f->addRow(tr("Scrollback lines"), boundSpin(QStringLiteral("display/scrollback"), 100, 100000, 5000, 500));
        f->addRow(boundCheck(tr("Strip mIRC colours from messages"), QStringLiteral("display/stripColors"), false));
        f->addRow(boundCheck(tr("Colour nicknames"), QStringLiteral("display/colorNicks"), true));
        f->addRow(boundCheck(tr("Wrap long lines"), QStringLiteral("display/wrap"), true));
        f->addRow(boundCheck(tr("Inline link/media previews (via Lurker)"), QStringLiteral("display/inlinePreviews"),
                             true));
        f->addRow(tr("Background colour"), boundColorField(QStringLiteral("display/bgColor")));
        f->addRow(tr("Background image"),
                  boundFileField(QStringLiteral("display/bgImage"), tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)")));
    });
    QTreeWidgetItem* displayNode = addCategory(nullptr, tr("Display"), displayTop);
    addCategory(displayNode, tr("Options"),
                buildPage(tr("Display Options"), tr("Window behaviour."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Show the switchbar"), QStringLiteral("display/switchbar"), true));
                    f->addRow(boundCheck(tr("Show the networks tree"), QStringLiteral("display/treebar"), true));
                    f->addRow(boundCheck(tr("Show the menu bar"), QStringLiteral("display/menubar"), true));
                    f->addRow(boundCheck(tr("Flash window on highlight"), QStringLiteral("display/flash"), true));
                    f->addRow(boundCheck(tr("Show icons in the nick list"), QStringLiteral("display/nicklistIcons"),
                                         true));
                }));

    // Colors page — the 16 mIRC palette swatches, editable, with named schemes.
    {
        auto* page = new QWidget;
        auto* v = new QVBoxLayout(page);
        auto* title = new QLabel(tr("Colors"));
        QFont tf = title->font();
        tf.setBold(true);
        tf.setPointSize(tf.pointSize() + 2);
        title->setFont(tf);
        v->addWidget(title);
        v->addWidget(new QLabel(tr("The 16 colours used by mIRC colour codes in messages. Click a swatch to change it.")));
        auto* grid = new QGridLayout;
        QSettings s;
        auto swatches = std::make_shared<QVector<QToolButton*>>();
        auto paint = [](QToolButton* btn, const QColor& c) {
            btn->setStyleSheet(QStringLiteral("QToolButton { background:%1; border:1px solid #555; }").arg(c.name()));
            btn->setProperty("colorValue", c.name());
        };
        for (int i = 0; i < 16; ++i) {
            const QString def = QString::fromLatin1(kMircColors[i]);
            QColor col(s.value(QStringLiteral("colors/%1").arg(i), def).toString());
            auto* btn = new QToolButton(page);
            btn->setFixedSize(34, 26);
            btn->setAutoRaise(false);
            btn->setToolTip(tr("Colour %1").arg(i));
            paint(btn, col);
            connect(btn, &QToolButton::clicked, page, [btn, paint, i] {
                const QColor cur(btn->property("colorValue").toString());
                const QColor chosen = QColorDialog::getColor(cur, btn, tr("Colour %1").arg(i));
                if (chosen.isValid()) {
                    paint(btn, chosen);
                    QSettings().setValue(QStringLiteral("colors/%1").arg(i), chosen.name());
                }
            });
            swatches->append(btn);
            grid->addWidget(new QLabel(QString::number(i)), i / 8 * 2, i % 8, Qt::AlignHCenter);
            grid->addWidget(btn, i / 8 * 2 + 1, i % 8);
        }
        v->addLayout(grid);

        // Apply a full 16-colour palette to the swatches + settings; read current.
        auto applyPalette = [swatches, paint](const QStringList& cols) {
            for (int i = 0; i < 16 && i < cols.size(); ++i) {
                const QColor c(cols[i]);
                if (!c.isValid()) continue;
                paint((*swatches)[i], c);
                QSettings().setValue(QStringLiteral("colors/%1").arg(i), c.name());
            }
        };
        auto current = [swatches] {
            QStringList o;
            for (QToolButton* b : *swatches) o << b->property("colorValue").toString();
            return o;
        };

        // Scheme row: pick/load a saved palette, save the current one, or delete.
        auto* schemeRow = new QHBoxLayout;
        schemeRow->addWidget(new QLabel(tr("Scheme:")));
        auto* combo = new QComboBox(page);
        combo->addItems(QSettings().value(QStringLiteral("schemes/_names")).toStringList());
        schemeRow->addWidget(combo, 1);
        auto* loadBtn = new QPushButton(tr("Load"), page);
        auto* saveBtn = new QPushButton(tr("Save As…"), page);
        auto* delBtn = new QPushButton(tr("Delete"), page);
        auto* resetBtn = new QPushButton(tr("Defaults"), page);
        schemeRow->addWidget(loadBtn);
        schemeRow->addWidget(saveBtn);
        schemeRow->addWidget(delBtn);
        schemeRow->addWidget(resetBtn);
        v->addLayout(schemeRow);

        connect(loadBtn, &QPushButton::clicked, page, [combo, applyPalette] {
            const QString name = combo->currentText();
            if (name.isEmpty()) return;
            applyPalette(QSettings().value(QStringLiteral("schemes/%1").arg(name)).toStringList());
        });
        connect(saveBtn, &QPushButton::clicked, page, [combo, current] {
            bool ok = false;
            const QString name =
                QInputDialog::getText(combo, tr("Save Scheme"), tr("Name:"), QLineEdit::Normal, QString(), &ok)
                    .trimmed();
            if (!ok || name.isEmpty()) return;
            QSettings st;
            st.setValue(QStringLiteral("schemes/%1").arg(name), current());
            QStringList names = st.value(QStringLiteral("schemes/_names")).toStringList();
            if (!names.contains(name)) {
                names << name;
                st.setValue(QStringLiteral("schemes/_names"), names);
                combo->addItem(name);
            }
            combo->setCurrentText(name);
        });
        connect(delBtn, &QPushButton::clicked, page, [combo] {
            const QString name = combo->currentText();
            if (name.isEmpty()) return;
            QSettings st;
            st.remove(QStringLiteral("schemes/%1").arg(name));
            QStringList names = st.value(QStringLiteral("schemes/_names")).toStringList();
            names.removeAll(name);
            st.setValue(QStringLiteral("schemes/_names"), names);
            combo->removeItem(combo->currentIndex());
        });
        connect(resetBtn, &QPushButton::clicked, page, [applyPalette] {
            QStringList def;
            for (int i = 0; i < 16; ++i) def << QString::fromLatin1(kMircColors[i]);
            applyPalette(def);
        });

        v->addStretch(1);
        addCategory(displayNode, tr("Colors"), page);
    }

    // ---- Sound ---------------------------------------------------------
    auto* soundTop = buildPage(tr("Sound"), tr("Event sounds and requests."), [&](QFormLayout* f) {
        f->addRow(boundCheck(tr("Enable event sounds"), QStringLiteral("sound/enabled"), false));
        f->addRow(boundCheck(tr("Beep on highlight"), QStringLiteral("sound/beepHighlight"), true));
        f->addRow(boundCheck(tr("Beep on private message"), QStringLiteral("sound/beepQuery"), false));
    });
    QTreeWidgetItem* soundNode = addCategory(nullptr, tr("Sound"), soundTop);
    addCategory(soundNode, tr("Requests"),
                buildPage(tr("Sound Requests"), tr("Incoming !nick file.wav sound requests."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Accept sound requests"), QStringLiteral("sound/accept"), false));
                    f->addRow(boundCheck(tr("Play requested sounds"), QStringLiteral("sound/play"), false));
                    f->addRow(tr("Sounds folder"), boundEdit(QStringLiteral("sound/dir")));
                }));

    // ---- Mouse ---------------------------------------------------------
    addCategory(nullptr, tr("Mouse"),
                buildPage(tr("Mouse"), tr("Double-click and clipboard behaviour."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Double-click nick to query"), QStringLiteral("mouse/dblQuery"), true));
                    f->addRow(boundCheck(tr("Double-click channel to join"), QStringLiteral("mouse/dblJoin"), true));
                    f->addRow(boundCheck(tr("Copy on select"), QStringLiteral("mouse/copyOnSelect"), false));
                    f->addRow(boundCheck(tr("Middle-click pastes"), QStringLiteral("mouse/middlePaste"), true));
                }));

    // ---- Logging -------------------------------------------------------
    addCategory(nullptr, tr("Logging"),
                buildPage(tr("Logging"), tr("Save conversations to disk."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Log channels"), QStringLiteral("irc/logChannels"), false));
                    f->addRow(boundCheck(tr("Log private queries"), QStringLiteral("irc/logQueries"), false));
                    f->addRow(tr("Log folder"), boundEdit(QStringLiteral("irc/logDir")));
                    f->addRow(boundCheck(tr("Timestamp log lines"), QStringLiteral("irc/logTimestamp"), true));
                    f->addRow(boundCheck(tr("Reload log when window opens"), QStringLiteral("irc/logReload"), true));
                }));

    // ---- Other ---------------------------------------------------------
    auto* otherTop = buildPage(tr("Other"), tr("Everything else."), [&](QFormLayout* f) {
        f->addRow(boundCheck(tr("Check for updates"), QStringLiteral("other/checkUpdates"), true));
        f->addRow(boundCheck(tr("Minimise to tray"), QStringLiteral("other/tray"), false));
        f->addRow(tr("Command character"), boundEdit(QStringLiteral("other/commandChar"), QStringLiteral("/")));
    });
    QTreeWidgetItem* otherNode = addCategory(nullptr, tr("Other"), otherTop);
    addCategory(otherNode, tr("Confirm"),
                buildPage(tr("Confirm"), tr("Ask before potentially destructive actions."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Confirm on exit"), QStringLiteral("confirm/exit"), true));
                    f->addRow(boundCheck(tr("Confirm on disconnect"), QStringLiteral("confirm/disconnect"), false));
                    f->addRow(boundCheck(tr("Confirm closing a channel"), QStringLiteral("confirm/closeChannel"),
                                         false));
                }));
    addCategory(otherNode, tr("Lock"),
                buildPage(tr("Lock"), tr("Restrict risky script/DCC operations."), [&](QFormLayout* f) {
                    f->addRow(boundCheck(tr("Lock DCC server"), QStringLiteral("lock/dccServer"), false));
                    f->addRow(boundCheck(tr("Disable /run and $com"), QStringLiteral("lock/disableRun"), false));
                    f->addRow(boundCheck(tr("Prompt before loading scripts"), QStringLiteral("lock/promptScripts"),
                                         false));
                }));

    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
        if (cur) m_stack->setCurrentIndex(cur->data(0, Qt::UserRole).toInt());
    });
    m_tree->expandAll();
    m_tree->setCurrentItem(connectNode);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* importBtn = buttons->addButton(tr("Import..."), QDialogButtonBox::ActionRole);
    auto* exportBtn = buttons->addButton(tr("Export..."), QDialogButtonBox::ActionRole);
    outer->addWidget(buttons);
    connect(importBtn, &QPushButton::clicked, this, &OptionsDialog::importTheme);
    connect(exportBtn, &QPushButton::clicked, this, &OptionsDialog::exportTheme);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    load();
}

QWidget* OptionsDialog::buildPage(const QString& title, const QString& blurb,
                                  const std::function<void(QFormLayout*)>& body) {
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    auto* titleLabel = new QLabel(title);
    QFont tf = titleLabel->font();
    tf.setBold(true);
    tf.setPointSize(tf.pointSize() + 2);
    titleLabel->setFont(tf);
    v->addWidget(titleLabel);
    if (!blurb.isEmpty()) {
        auto* b = new QLabel(blurb);
        b->setWordWrap(true);
        b->setStyleSheet(QStringLiteral("color: palette(mid);"));
        v->addWidget(b);
    }
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    v->addWidget(line);
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    v->addLayout(form);
    body(form);
    v->addStretch(1);
    return page;
}

QLineEdit* OptionsDialog::boundEdit(const QString& key, const QString& def) {
    auto* e = new QLineEdit;
    m_bindings.push_back({e, key, def, false});
    return e;
}

QCheckBox* OptionsDialog::boundCheck(const QString& label, const QString& key, bool def) {
    auto* c = new QCheckBox(label);
    m_bindings.push_back({c, key, def, false});
    return c;
}

QSpinBox* OptionsDialog::boundSpin(const QString& key, int lo, int hi, int def, int step) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    m_bindings.push_back({s, key, def, false});
    return s;
}

QComboBox* OptionsDialog::boundCombo(const QString& key, const QStringList& items, const QString& def) {
    auto* c = new QComboBox;
    c->addItems(items);
    m_bindings.push_back({c, key, def, false});
    return c;
}

QWidget* OptionsDialog::boundColorField(const QString& key, const QString& def) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit;
    edit->setPlaceholderText(tr("theme default"));
    m_bindings.push_back({edit, key, def, false});
    auto* pick = new QPushButton(tr("Pick..."));
    auto* clear = new QPushButton(tr("Clear"));
    h->addWidget(edit, 1);
    h->addWidget(pick);
    h->addWidget(clear);
    connect(pick, &QPushButton::clicked, row, [edit] {
        const QColor cur = edit->text().isEmpty() ? QColor(Qt::white) : QColor(edit->text());
        const QColor c = QColorDialog::getColor(cur, edit, tr("Pick a colour"));
        if (c.isValid()) edit->setText(c.name());
    });
    connect(clear, &QPushButton::clicked, edit, &QLineEdit::clear);
    return row;
}

QWidget* OptionsDialog::boundFileField(const QString& key, const QString& filter) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    auto* edit = new QLineEdit;
    edit->setPlaceholderText(tr("none"));
    m_bindings.push_back({edit, key, QString(), false});
    auto* browse = new QPushButton(tr("Browse..."));
    auto* clear = new QPushButton(tr("Clear"));
    h->addWidget(edit, 1);
    h->addWidget(browse);
    h->addWidget(clear);
    connect(browse, &QPushButton::clicked, row, [edit, filter] {
        const QString p = QFileDialog::getOpenFileName(edit, tr("Choose a file"), edit->text(), filter);
        if (!p.isEmpty()) edit->setText(p);
    });
    connect(clear, &QPushButton::clicked, edit, &QLineEdit::clear);
    return row;
}

void OptionsDialog::exportTheme() {
    QString path = QFileDialog::getSaveFileName(this, tr("Export Theme"), QStringLiteral("my-theme.lrktheme"),
                                                tr("Lurker theme (*.lrktheme *.ini)"));
    if (path.isEmpty()) return;
    save();  // make sure on-screen edits are persisted before we copy them out
    QSettings src;
    QSettings out(path, QSettings::IniFormat);
    for (const QString& k : src.allKeys()) {
        if (k == QStringLiteral("theme") || k.startsWith(QStringLiteral("display/")) ||
            k.startsWith(QStringLiteral("colors/"))) {
            out.setValue(k, src.value(k));
        }
    }
    out.sync();
}

void OptionsDialog::importTheme() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Import Theme"), QString(), tr("Lurker theme (*.lrktheme *.ini)"));
    if (path.isEmpty()) return;
    QSettings in(path, QSettings::IniFormat);
    QSettings dst;
    for (const QString& k : in.allKeys()) dst.setValue(k, in.value(k));
    dst.sync();
    load();  // refresh every bound widget from the imported values
    applyTheme(*qApp, dst.value(QStringLiteral("theme"), QStringLiteral("default")).toString());
}

QTreeWidgetItem* OptionsDialog::addCategory(QTreeWidgetItem* parent, const QString& name, QWidget* page) {
    const int index = m_stack->addWidget(page);
    auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, name);
    item->setData(0, Qt::UserRole, index);
    return item;
}

void OptionsDialog::selectPage(const QString& name) {
    const auto items = m_tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
    if (!items.isEmpty()) m_tree->setCurrentItem(items.first());
}

void OptionsDialog::load() {
    QSettings s;
    for (const Binding& b : m_bindings) {
        const QVariant v = s.value(b.key, b.def);
        if (auto* e = qobject_cast<QLineEdit*>(b.widget)) {
            e->setText(v.toString());
        } else if (auto* c = qobject_cast<QCheckBox*>(b.widget)) {
            c->setChecked(v.toBool());
        } else if (auto* sp = qobject_cast<QSpinBox*>(b.widget)) {
            sp->setValue(v.toInt());
        } else if (auto* fc = qobject_cast<QFontComboBox*>(b.widget)) {
            fc->setCurrentFont(QFont(v.toString()));
        } else if (auto* cb = qobject_cast<QComboBox*>(b.widget)) {
            if (b.useData) {
                cb->setCurrentIndex(qMax(0, cb->findData(v.toString())));
            } else {
                const int idx = cb->findText(v.toString());
                cb->setCurrentIndex(idx >= 0 ? idx : 0);
            }
        }
    }
}

void OptionsDialog::save() {
    QSettings s;
    for (const Binding& b : m_bindings) {
        if (auto* e = qobject_cast<QLineEdit*>(b.widget)) {
            s.setValue(b.key, e->text());
        } else if (auto* c = qobject_cast<QCheckBox*>(b.widget)) {
            s.setValue(b.key, c->isChecked());
        } else if (auto* sp = qobject_cast<QSpinBox*>(b.widget)) {
            s.setValue(b.key, sp->value());
        } else if (auto* fc = qobject_cast<QFontComboBox*>(b.widget)) {
            s.setValue(b.key, fc->currentFont().family());
        } else if (auto* cb = qobject_cast<QComboBox*>(b.widget)) {
            s.setValue(b.key, b.useData ? cb->currentData().toString() : cb->currentText());
        }
    }
    applyTheme(*qApp, s.value(QStringLiteral("theme"), QStringLiteral("default")).toString());
}

}  // namespace lurker::gui
