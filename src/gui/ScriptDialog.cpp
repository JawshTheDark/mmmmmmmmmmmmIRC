// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/ScriptDialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTreeWidget>

namespace lurker::gui {

namespace {
// mIRC dialog units → pixels. Windows dialog base units are font-derived; this
// fixed factor reproduces the proportions closely enough for script layouts.
constexpr double kSx = 1.55;
constexpr double kSy = 1.65;
int px(int unit, double scale) { return static_cast<int>(unit * scale + 0.5); }

bool isNumericTok(const QString& s) {
    bool ok = false;
    s.toInt(&ok);
    return ok && !s.isEmpty();
}
// mdx/ListView columns are tab-delimited ($chr(9)); split and trim each column.
QStringList splitTabs(const QString& s) {
    QStringList out;
    const QStringList parts = s.split(QLatin1Char('\t'));
    for (const QString& p : parts) out << p.trimmed();
    return out;
}
}  // namespace

ScriptDialog::ScriptDialog(const lurker::msl::DialogTable& table, QString instance,
                           std::function<void(const QString&, int)> eventSink, QWidget* parent)
    : QDialog(parent), m_instance(std::move(instance)), m_sink(std::move(eventSink)) {
    setWindowTitle(QString::fromStdString(table.title.empty() ? table.name : table.title));
    setFixedSize(px(table.w, kSx), px(table.h, kSy));

    for (const lurker::msl::DialogControl& c : table.controls) {
        const QString cap = QString::fromStdString(c.caption);
        QWidget* w = nullptr;
        if (c.type == "text" || c.type == "link") {
            auto* label = new QLabel(cap, this);
            label->setWordWrap(true);
            if (c.type == "link") label->setTextFormat(Qt::RichText);
            w = label;
        } else if (c.type == "edit") {
            if (c.hasStyle("multi")) {
                auto* e = new QPlainTextEdit(this);
                e->setPlainText(cap);
                e->setReadOnly(c.hasStyle("read"));
                w = e;
            } else {
                auto* e = new QLineEdit(cap, this);
                if (c.hasStyle("pass")) e->setEchoMode(QLineEdit::Password);
                e->setReadOnly(c.hasStyle("read"));
                const int id = c.id;
                connect(e, &QLineEdit::textEdited, this, [this, id] { if (m_sink) m_sink(QStringLiteral("edit"), id); });
                w = e;
            }
        } else if (c.type == "button") {
            auto* b = new QPushButton(cap, this);
            const int id = c.id;
            const bool ok = c.hasStyle("ok"), cancel = c.hasStyle("cancel");
            connect(b, &QPushButton::clicked, this, [this, id, ok, cancel] {
                if (m_sink) m_sink(QStringLiteral("sclick"), id);
                if (ok) accept();
                else if (cancel) reject();
            });
            w = b;
        } else if (c.type == "check") {
            auto* cb = new QCheckBox(cap, this);
            const int id = c.id;
            connect(cb, &QCheckBox::toggled, this, [this, id] { if (m_sink) m_sink(QStringLiteral("sclick"), id); });
            w = cb;
        } else if (c.type == "radio") {
            auto* rb = new QRadioButton(cap, this);
            const int id = c.id;
            connect(rb, &QRadioButton::toggled, this, [this, id] { if (m_sink) m_sink(QStringLiteral("sclick"), id); });
            w = rb;
        } else if (c.type == "list") {
            auto* lw = new QListWidget(this);
            const int id = c.id;
            connect(lw, &QListWidget::itemSelectionChanged, this, [this, id] { if (m_sink) m_sink(QStringLiteral("sclick"), id); });
            connect(lw, &QListWidget::itemDoubleClicked, this, [this, id](QListWidgetItem*) { if (m_sink) m_sink(QStringLiteral("dclick"), id); });
            w = lw;
        } else if (c.type == "combo") {
            auto* cbx = new QComboBox(this);
            cbx->setEditable(c.hasStyle("edit"));
            w = cbx;
        } else if (c.type == "box") {
            w = new QGroupBox(cap, this);
        } else if (c.type == "icon" || c.type == "image") {
            w = new QLabel(this);  // image loading comes with the theming work
        }
        if (!w) continue;
        w->setGeometry(px(c.x, kSx), px(c.y, kSy), px(c.w, kSx), px(c.h, kSy));
        if (c.id > 0) m_byId.insert(c.id, w);
    }

    // Fire the init event once the dialog is built.
    if (m_sink) {
        auto sink = m_sink;
        QMetaObject::invokeMethod(this, [sink] { sink(QStringLiteral("init"), 0); }, Qt::QueuedConnection);
    }
}

void ScriptDialog::applyMdxControl(int id, const QString& type, const QStringList& styles) {
    QWidget* old = m_byId.value(id);
    if (!old) return;
    const QString t = type.toLower();
    // ListView → a multi-column report table (mIRC addons use this for grids).
    if (t == QLatin1String("listview") || t == QLatin1String("report")) {
        if (qobject_cast<QTreeWidget*>(old)) return;  // already promoted
        const QRect geo = old->geometry();
        auto* tree = new QTreeWidget(this);
        tree->setGeometry(geo);
        tree->setRootIsDecorated(false);
        tree->setUniformRowHeights(true);
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        tree->setSelectionBehavior(QAbstractItemView::SelectRows);
        tree->setAllColumnsShowFocus(true);
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree->header()->setStretchLastSection(true);
        tree->setSortingEnabled(!styles.contains(QStringLiteral("nosortheader"), Qt::CaseInsensitive));
        if (styles.contains(QStringLiteral("grid"), Qt::CaseInsensitive))
            tree->setStyleSheet(QStringLiteral("QTreeView{gridline-color:palette(mid);}"
                                               "QTreeView::item{border-right:1px solid palette(mid);}"));
        tree->setColumnCount(1);
        const int cid = id;
        connect(tree, &QTreeWidget::itemSelectionChanged, this,
                [this, cid] { if (m_sink) m_sink(QStringLiteral("sclick"), cid); });
        connect(tree, &QTreeWidget::itemDoubleClicked, this,
                [this, cid](QTreeWidgetItem*, int) { if (m_sink) m_sink(QStringLiteral("dclick"), cid); });
        tree->show();
        old->deleteLater();
        m_byId.insert(id, tree);
    }
}

void ScriptDialog::applyDid(int id, const QString& sw, const QString& text) {
    QWidget* w = m_byId.value(id);
    if (!w) return;
    const bool reset = sw.contains('r');
    const bool add = sw.contains('a');

    // ListView table (mdx-promoted control): header ops (-i) and tab-delimited rows.
    if (auto* tree = qobject_cast<QTreeWidget*>(w)) {
        if (sw.contains('i')) {
            // did -i <id> <line> <subcmd> <args>  (mdx: headertext / headerdims)
            const QStringList t = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            int k = 0;
            while (k < t.size() && isNumericTok(t[k])) ++k;  // skip the mdx line index
            const QString sub = k < t.size() ? t[k].toLower() : QString();
            const QStringList rest = t.mid(k + 1);
            if (sub == QLatin1String("headertext")) {
                const QStringList cols = splitTabs(rest.join(QLatin1Char(' ')));
                tree->setColumnCount(qMax(1, cols.size()));
                tree->setHeaderLabels(cols);
            } else if (sub == QLatin1String("headerdims")) {
                for (int c = 0; c < rest.size(); ++c) tree->setColumnWidth(c, rest[c].toInt());
            }
            return;
        }
        if (reset) tree->clear();
        if (sw.contains('d')) {  // did -d <id> <N>
            const int row = text.trimmed().toInt();
            if (row >= 1 && row <= tree->topLevelItemCount()) delete tree->takeTopLevelItem(row - 1);
            return;
        }
        if (add && !text.isEmpty()) {
            const QStringList cols = splitTabs(text);
            if (tree->columnCount() < cols.size()) tree->setColumnCount(cols.size());
            new QTreeWidgetItem(tree, cols);
        }
        return;
    }

    if (auto* e = qobject_cast<QLineEdit*>(w)) {
        if (reset) e->clear();
        else e->setText(text);
    } else if (auto* e = qobject_cast<QPlainTextEdit*>(w)) {
        if (reset) e->clear();
        else if (add) e->appendPlainText(text);
        else e->setPlainText(text);
    } else if (auto* lw = qobject_cast<QListWidget*>(w)) {
        if (reset) lw->clear();
        if (add && !text.isEmpty()) lw->addItem(text);
    } else if (auto* cb = qobject_cast<QComboBox*>(w)) {
        if (reset) cb->clear();
        if (add && !text.isEmpty()) cb->addItem(text);
    } else if (auto* lbl = qobject_cast<QLabel*>(w)) {
        if (!reset) lbl->setText(text);
        else lbl->clear();
    } else if (auto* btn = qobject_cast<QAbstractButton*>(w)) {
        if (sw.contains('c')) btn->setChecked(true);
        else if (sw.contains('u')) btn->setChecked(false);
        else if (!text.isEmpty()) btn->setText(text);
    }
    if (sw.contains('e')) w->setEnabled(true);
    if (sw.contains('b')) w->setEnabled(false);
    if (sw.contains('h')) w->hide();
    if (sw.contains('v')) w->show();
}

QString ScriptDialog::query(int id, const QString& prop) const {
    QWidget* w = m_byId.value(id);
    if (!w) return {};
    if (auto* tree = qobject_cast<QTreeWidget*>(w)) {
        if (prop == QLatin1String("lines")) return QString::number(tree->topLevelItemCount());
        if (prop == QLatin1String("sel")) {
            QTreeWidgetItem* cur = tree->currentItem();
            const int row = cur ? tree->indexOfTopLevelItem(cur) : -1;
            return row >= 0 ? QString::number(row + 1) : QString();
        }
        // .text / .seltext / .text:N → column 0 of the selected (or Nth) row.
        int row = -1;
        const int colon = prop.indexOf(QLatin1Char(':'));
        if (colon >= 0) {
            row = prop.mid(colon + 1).toInt() - 1;
        } else if (tree->currentItem()) {
            row = tree->indexOfTopLevelItem(tree->currentItem());
        }
        if (row >= 0 && row < tree->topLevelItemCount()) return tree->topLevelItem(row)->text(0);
        return {};
    }
    if (auto* e = qobject_cast<QLineEdit*>(w)) return e->text();
    if (auto* e = qobject_cast<QPlainTextEdit*>(w)) {
        if (prop == QLatin1String("lines")) return QString::number(e->document()->blockCount());
        return e->toPlainText();
    }
    if (auto* lw = qobject_cast<QListWidget*>(w)) {
        if (prop == QLatin1String("lines")) return QString::number(lw->count());
        if (prop == QLatin1String("sel")) return QString::number(lw->currentRow() + 1);
        if (lw->currentItem()) return lw->currentItem()->text();
        return {};
    }
    if (auto* cb = qobject_cast<QComboBox*>(w)) return cb->currentText();
    if (auto* btn = qobject_cast<QAbstractButton*>(w)) {
        if (prop == QLatin1String("state")) return btn->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
        return btn->text();
    }
    if (auto* lbl = qobject_cast<QLabel*>(w)) return lbl->text();
    return {};
}

void ScriptDialog::closeEvent(QCloseEvent* event) {
    if (!m_closeFired && m_sink) {
        m_closeFired = true;
        m_sink(QStringLiteral("close"), 0);
    }
    QDialog::closeEvent(event);
}

}  // namespace lurker::gui
