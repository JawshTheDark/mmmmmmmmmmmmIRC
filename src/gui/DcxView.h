// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// DcxView renders a DCX (Dynamic Controls eXtension) dialog layout. DCX is a
// third-party mIRC DLL that lays out rich controls with a nested pane/weight
// box model — not mIRC's absolute dialog units — so scripts describe dialogs in
// XML (loaded via /dcxml) rather than a dialog table. We map each container
// (pane / panel) to a QBoxLayout chosen by its `cascade` direction, empty
// <pane/> elements to stretch spacers, and each control to the matching Qt
// widget. One DcxView fills the host ScriptDialog once /dcxml takes it over.
//
// Phase 1: parse the XML, build the layout, and render the common controls
// (panel, pane, text, link, box, line, divider, edit, list, listview,
// treeview, tab, button, check, radio, progressbar, image); the richer
// controls (toolbar, rebar, trackbar, colorcombo, richedit, updown, webctrl,
// embedded @window, …) render as labelled placeholders until later phases.
#pragma once

#include <QHash>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <vector>

class QXmlStreamReader;

namespace lurker::gui {

class DcxView : public QWidget {
    Q_OBJECT
public:
    explicit DcxView(QWidget* parent = nullptr);

    // Parse `xmlPath` and build the <dialog name="dialogName"> subtree into this
    // widget. Returns the dialog's requested pixel size (the XML's w/h), or an
    // invalid QSize if the file or that dialog could not be loaded.
    QSize load(const QString& xmlPath, const QString& dialogName);

    bool loaded() const { return m_loaded; }
    QString caption() const { return m_caption; }  // the <dialog caption="...">

    // A control widget by its DCX id (0 if none). Used by /xdid and $xdid.
    QWidget* control(int id) const { return m_byId.value(id); }

    // Apply an /xdid control op (#19 phase 1b). `sw` is the switch token ("-v",
    // "-a", "-r", …); `args` the text after it. Handles the common ops:
    //   -v set text/value, -a add an item, -r clear, -c check, -h hide, -s show.
    void xdid(const QString& sw, int id, const QString& args);

    // The text of a control, for `$xdid(dialog,id).text` (edit/label); empty else.
    QString controlText(int id) const;

signals:
    // An interactive control fired a DCX event (#19 phase 2): `event` is the DCX
    // event name ("sclick", "selchange", …), `id` the control, `arg` any extra
    // (e.g. a 1-based selected row for selchange). The owner routes it to the
    // dialog's marked event alias (dcx Mark).
    void controlEvent(int id, const QString& event, const QString& arg);

private:
    void connectControlEvents(QWidget* w, int id);  // wire a control's Qt signals to controlEvent


    // One parsed node of the DCX tree: a container (pane/panel with children),
    // a leaf control, an empty <pane/> spacer, or an <item>.
    struct Node {
        QString type;            // control type ("panel", "button", …); "pane" for <pane>
        int id = 0;
        int width = -1;          // fixed px size in the cascade cross/again axis, -1 = auto
        int height = -1;
        int weight = -1;         // stretch share along the cascade, -1 = unspecified
        bool spacer = false;     // empty <pane/> — a stretch spacer
        QString cascade;         // "h" | "v" (container child flow); default "v"
        QString caption;
        QString styles;
        QString margin;          // "l t r b"
        QString cells;           // statusbar/rebar cell widths
        QString text;            // element text (edit body, item text)
        QStringList items;       // flat <item> captions for list/toolbar/etc.
        std::vector<Node> children;  // child controls/panes
    };

    Node parseElement(QXmlStreamReader& xml);   // read one control/pane subtree
    QWidget* build(const Node& n, QWidget* parent);  // node -> widget (recursive)
    QWidget* buildContainer(const Node& n);     // pane/panel -> widget + box layout

    bool m_loaded = false;
    QString m_caption;
    QHash<int, QWidget*> m_byId;
};

}  // namespace lurker::gui
