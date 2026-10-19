// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/DcxView.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QFile>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace lurker::gui {

namespace {

// Parse a "l t r b" margin string into four ints (missing entries -> 0).
void parseMargin(const QString& s, int& l, int& t, int& r, int& b) {
    const QStringList p = s.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    l = p.value(0).toInt();
    t = p.value(1).toInt();
    r = p.value(2).toInt();
    b = p.value(3).toInt();
}

bool hasStyle(const QString& styles, const char* word) {
    const QStringList p = styles.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return p.contains(QLatin1String(word), Qt::CaseInsensitive);
}

}  // namespace

DcxView::DcxView(QWidget* parent) : QWidget(parent) {}

// Read one <control>/<pane> element (the reader is positioned on its start tag)
// and its subtree into a Node. Child <control>/<pane> recurse; <item> captions
// are collected flat; <styles>/<icons> and unknown tags are skipped.
DcxView::Node DcxView::parseElement(QXmlStreamReader& xml) {
    Node n;
    const QXmlStreamAttributes a = xml.attributes();
    n.type = xml.name().toString() == QLatin1String("pane")
                 ? QStringLiteral("pane")
                 : a.value(QLatin1String("type")).toString();
    n.id = a.value(QLatin1String("id")).toInt();
    n.width = a.hasAttribute(QLatin1String("width")) ? a.value(QLatin1String("width")).toInt() : -1;
    n.height = a.hasAttribute(QLatin1String("height")) ? a.value(QLatin1String("height")).toInt() : -1;
    n.weight = a.hasAttribute(QLatin1String("weight")) ? a.value(QLatin1String("weight")).toInt() : -1;
    n.cascade = a.value(QLatin1String("cascade")).toString();
    n.caption = a.value(QLatin1String("caption")).toString();
    n.styles = a.value(QLatin1String("styles")).toString();
    n.margin = a.value(QLatin1String("margin")).toString();
    n.cells = a.value(QLatin1String("cells")).toString();

    const QString tag = xml.name().toString();
    while (!xml.atEnd()) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            const QString child = xml.name().toString();
            if (child == QLatin1String("control") || child == QLatin1String("pane")) {
                n.children.push_back(parseElement(xml));
            } else if (child == QLatin1String("item")) {
                const QString cap = xml.attributes().value(QLatin1String("caption")).toString();
                const QString txt = xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                n.items << (cap.isEmpty() ? txt : cap);
            } else {
                xml.skipCurrentElement();  // styles, icons, blah, …
            }
        } else if (tok == QXmlStreamReader::Characters && !xml.isWhitespace()) {
            n.text += xml.text().toString();
        } else if (tok == QXmlStreamReader::EndElement && xml.name().toString() == tag) {
            break;
        }
    }
    n.text = n.text.trimmed();
    // An empty <pane/> (no children, items, size or caption) is a stretch spacer.
    if (n.type == QLatin1String("pane") && n.children.empty() && n.items.isEmpty() &&
        n.width < 0 && n.height < 0 && n.caption.isEmpty())
        n.spacer = true;
    return n;
}

QSize DcxView::load(const QString& xmlPath, const QString& dialogName) {
    QFile f(xmlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QSize();
    QXmlStreamReader xml(&f);

    // Find <dialogs> → the <dialog name="dialogName"> and parse its children.
    Node root;
    root.type = QStringLiteral("pane");  // the dialog body is a vertical container
    int w = -1, h = -1;
    bool found = false;
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) continue;
        if (xml.name().toString() != QLatin1String("dialog")) continue;
        const auto a = xml.attributes();
        if (a.value(QLatin1String("name")).toString() != dialogName) {
            xml.skipCurrentElement();
            continue;
        }
        found = true;
        w = a.value(QLatin1String("w")).toInt();
        h = a.value(QLatin1String("h")).toInt();
        m_caption = a.value(QLatin1String("caption")).toString();
        root.margin = a.value(QLatin1String("margin")).toString();
        // Parse the dialog's direct children (controls / panes).
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                const QString child = xml.name().toString();
                if (child == QLatin1String("control") || child == QLatin1String("pane"))
                    root.children.push_back(parseElement(xml));
                else
                    xml.skipCurrentElement();  // <styles>, <icons>
            } else if (tok == QXmlStreamReader::EndElement &&
                       xml.name().toString() == QLatin1String("dialog")) {
                break;
            }
        }
        break;
    }
    if (!found || xml.hasError()) return QSize();

    // Reset any previous build so load() is idempotent: a re-init (dialog loaded
    // again) must not stack a second layout on this DcxView or leave m_byId
    // pointing at orphaned, invisible widgets.
    m_byId.clear();
    qDeleteAll(findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly));
    delete layout();

    // Build the widget tree: this DcxView is the root container.
    QWidget* body = buildContainer(root);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(body);
    m_loaded = true;
    return (w > 0 && h > 0) ? QSize(w, h) : QSize(400, 300);
}

QWidget* DcxView::buildContainer(const Node& n) {
    // A captioned panel reads as a titled group box; otherwise a plain widget.
    QWidget* box = nullptr;
    QBoxLayout* lay = nullptr;
    const bool horizontal = n.cascade.compare(QLatin1String("h"), Qt::CaseInsensitive) == 0;
    if (!n.caption.isEmpty() && n.type == QLatin1String("panel")) {
        auto* gb = new QGroupBox(n.caption);
        lay = horizontal ? static_cast<QBoxLayout*>(new QHBoxLayout(gb)) : new QVBoxLayout(gb);
        box = gb;
    } else {
        box = new QWidget;
        lay = horizontal ? static_cast<QBoxLayout*>(new QHBoxLayout(box)) : new QVBoxLayout(box);
    }
    int ml = 2, mt = 2, mr = 2, mb = 2;
    if (!n.margin.isEmpty()) parseMargin(n.margin, ml, mt, mr, mb);
    lay->setContentsMargins(ml, mt, mr, mb);
    lay->setSpacing(2);

    for (const Node& c : n.children) {
        if (c.spacer) {
            lay->addStretch(c.weight > 0 ? c.weight : 1);
            continue;
        }
        QWidget* cw = build(c, box);
        if (!cw) continue;
        if (c.width > 0) cw->setFixedWidth(c.width);
        if (c.height > 0) cw->setFixedHeight(c.height);
        // Along the flow axis, a fixed-size child stays put (stretch 0); an
        // unsized child grows (explicit weight, else 1).
        const bool fixedFlow = horizontal ? (c.width > 0) : (c.height > 0);
        const int stretch = fixedFlow ? 0 : (c.weight >= 0 ? c.weight : 1);
        lay->addWidget(cw, stretch);
    }
    if (box) box->setObjectName(QStringLiteral("dcxpanel"));
    return box;
}

QWidget* DcxView::build(const Node& n, QWidget* parent) {
    // Containers: pane / panel / divider / tab hold children.
    if (n.type == QLatin1String("tab")) {
        auto* tabs = new QTabWidget(parent);
        for (const Node& c : n.children)
            tabs->addTab(buildContainer(c), c.caption.isEmpty() ? QStringLiteral("Tab") : c.caption);
        if (n.id > 0) m_byId.insert(n.id, tabs);
        return tabs;
    }
    const bool container = (n.type == QLatin1String("pane") || n.type == QLatin1String("panel") ||
                            n.type == QLatin1String("divider")) &&
                           !n.children.empty();
    if (container) {
        QWidget* w = buildContainer(n);
        if (n.id > 0) m_byId.insert(n.id, w);
        return w;
    }

    // Leaf controls.
    QWidget* w = nullptr;
    const QString& t = n.type;
    if (t == QLatin1String("text")) {
        w = new QLabel(n.caption.isEmpty() ? n.text : n.caption, parent);
    } else if (t == QLatin1String("link")) {
        auto* l = new QLabel(QStringLiteral("<a href=\"%1\">%1</a>").arg(n.caption), parent);
        l->setOpenExternalLinks(true);
        w = l;
    } else if (t == QLatin1String("box")) {
        w = new QGroupBox(n.caption, parent);
    } else if (t == QLatin1String("line")) {
        auto* fr = new QFrame(parent);
        fr->setFrameShape(hasStyle(n.styles, "vertical") ? QFrame::VLine : QFrame::HLine);
        fr->setFrameShadow(QFrame::Sunken);
        w = fr;
    } else if (t == QLatin1String("edit")) {
        if (hasStyle(n.styles, "multi")) {
            auto* e = new QPlainTextEdit(n.text, parent);
            e->setReadOnly(hasStyle(n.styles, "readonly"));
            w = e;
        } else {
            auto* e = new QLineEdit(n.text.isEmpty() ? n.caption : n.text, parent);
            e->setReadOnly(hasStyle(n.styles, "readonly"));
            w = e;
        }
    } else if (t == QLatin1String("list")) {
        auto* lw = new QListWidget(parent);
        lw->addItems(n.items);
        w = lw;
    } else if (t == QLatin1String("listview")) {
        auto* tv = new QTreeWidget(parent);
        tv->setRootIsDecorated(false);  // report-style, flat rows
        tv->setHeaderHidden(true);      // columns come later via xdid -t
        w = tv;
    } else if (t == QLatin1String("treeview")) {
        auto* tv = new QTreeWidget(parent);
        tv->setHeaderHidden(true);
        for (const QString& it : n.items) tv->addTopLevelItem(new QTreeWidgetItem(QStringList{it}));
        w = tv;
    } else if (t == QLatin1String("button")) {
        w = new QPushButton(n.caption, parent);
    } else if (t == QLatin1String("check")) {
        w = new QCheckBox(n.caption, parent);
    } else if (t == QLatin1String("radio")) {
        w = new QRadioButton(n.caption, parent);
    } else if (t == QLatin1String("pbar") || t == QLatin1String("progressbar")) {
        auto* pb = new QProgressBar(parent);
        pb->setRange(0, 100);
        pb->setValue(0);
        w = pb;
    } else {
        // image, statusbar, toolbar, rebar, trackbar, colorcombo, richedit,
        // ipaddress, updown, webctrl, scroll, window, … — labelled placeholder.
        auto* ph = new QLabel(QStringLiteral("[%1]").arg(t.isEmpty() ? QStringLiteral("?") : t), parent);
        ph->setAlignment(Qt::AlignCenter);
        ph->setFrameShape(QFrame::StyledPanel);
        ph->setMinimumSize(16, 12);
        w = ph;
    }
    if (w && n.id > 0) m_byId.insert(n.id, w);
    return w;
}

void DcxView::xdid(const QString& sw, int id, const QString& args) {
    QWidget* w = m_byId.value(id);
    if (!w) return;
    const QString a = args.trimmed();
    // -h hide / -s show (wizard-style panels).
    if (sw.contains(QLatin1Char('h'))) { w->hide(); return; }
    if (sw.contains(QLatin1Char('s'))) { w->show(); return; }
    // -r reset/clear a list or tree.
    if (sw.contains(QLatin1Char('r'))) {
        if (auto* lw = qobject_cast<QListWidget*>(w)) lw->clear();
        else if (auto* tw = qobject_cast<QTreeWidget*>(w)) tw->clear();
        return;
    }
    // -a / -A add an item. DCX args are "N [+flags] text"; take the trailing text.
    if (sw.contains(QLatin1Char('a'), Qt::CaseInsensitive)) {
        QString text = a.section(QLatin1Char(' '), 1);  // drop the leading position N
        if (text.startsWith(QLatin1Char('+')))          // drop optional +flags token
            text = text.section(QLatin1Char(' '), 1);
        if (text.isEmpty()) text = a;
        if (auto* lw = qobject_cast<QListWidget*>(w)) lw->addItem(text);
        else if (auto* tw = qobject_cast<QTreeWidget*>(w)) new QTreeWidgetItem(tw, QStringList(text));
        return;
    }
    // -c check/select.
    if (sw.contains(QLatin1Char('c'))) {
        const bool on = a != QLatin1String("0");
        if (auto* cb = qobject_cast<QCheckBox*>(w)) cb->setChecked(on);
        else if (auto* rb = qobject_cast<QRadioButton*>(w)) rb->setChecked(on);
        else if (auto* lw = qobject_cast<QListWidget*>(w)) {
            const int n = a.section(QLatin1Char(' '), 0, 0).toInt();
            if (n >= 1 && n <= lw->count()) lw->setCurrentRow(n - 1);
        }
        return;
    }
    // -v set text / value.
    if (sw.contains(QLatin1Char('v'))) {
        if (auto* e = qobject_cast<QLineEdit*>(w)) e->setText(args);
        else if (auto* te = qobject_cast<QPlainTextEdit*>(w)) te->setPlainText(args);
        else if (auto* l = qobject_cast<QLabel*>(w)) l->setText(args);
        else if (auto* pb = qobject_cast<QProgressBar*>(w)) pb->setValue(a.toInt());
        else if (auto* b = qobject_cast<QPushButton*>(w)) b->setText(args);
        return;
    }
}

QString DcxView::controlText(int id) const {
    QWidget* w = m_byId.value(id);
    if (auto* e = qobject_cast<QLineEdit*>(w)) return e->text();
    if (auto* te = qobject_cast<QPlainTextEdit*>(w)) return te->toPlainText();
    if (auto* l = qobject_cast<QLabel*>(w)) return l->text();
    if (auto* b = qobject_cast<QPushButton*>(w)) return b->text();
    return QString();
}

}  // namespace lurker::gui
