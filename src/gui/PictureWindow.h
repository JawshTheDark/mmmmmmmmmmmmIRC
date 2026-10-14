// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// A picture window (/window -p): a canvas scripts draw on with /drawrect,
// /drawtext, /drawline, /drawdot, /drawfill. Backed by a QImage so drawing
// persists across repaints and window resizes.
#pragma once

#include <QColor>
#include <QImage>
#include <QStringList>
#include <QWidget>

namespace lurker::gui {

class PictureWindow : public QWidget {
    Q_OBJECT
public:
    explicit PictureWindow(QWidget* parent = nullptr);

    // op is "rect","text","line","dot","fill","clear"; args are the /draw* params
    // after the window name (mIRC order). Colors are mIRC palette indices or r,g,b.
    void drawOp(const QString& op, const QStringList& args);

protected:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    QColor color(const QString& spec) const;
    void ensureCanvas(const QSize& size);
    QImage m_canvas;
};

}  // namespace lurker::gui
