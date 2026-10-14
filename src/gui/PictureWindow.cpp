// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/PictureWindow.h"

#include <QPainter>

#include "core/IrcFormat.h"

namespace lurker::gui {

PictureWindow::PictureWindow(QWidget* parent) : QWidget(parent) {
    setAutoFillBackground(true);
    m_canvas = QImage(640, 400, QImage::Format_ARGB32_Premultiplied);
    m_canvas.fill(Qt::black);
}

void PictureWindow::ensureCanvas(const QSize& size) {
    if (size.width() <= m_canvas.width() && size.height() <= m_canvas.height()) return;
    QImage bigger(size.expandedTo(m_canvas.size()), QImage::Format_ARGB32_Premultiplied);
    bigger.fill(Qt::black);
    QPainter p(&bigger);
    p.drawImage(0, 0, m_canvas);
    m_canvas = bigger;
}

QColor PictureWindow::color(const QString& spec) const {
    if (spec.contains(',')) {
        const QStringList rgb = spec.split(',');
        if (rgb.size() == 3) return QColor(rgb[0].toInt(), rgb[1].toInt(), rgb[2].toInt());
    }
    bool ok = false;
    const int idx = spec.toInt(&ok);
    if (ok) {
        if (const auto c = lurker::paletteColor(idx)) return QColor(c->r, c->g, c->b);
    }
    return QColor(spec);  // named colour fallback
}

void PictureWindow::drawOp(const QString& op, const QStringList& a) {
    QPainter p(&m_canvas);
    p.setRenderHint(QPainter::Antialiasing, true);
    auto n = [&](int i) { return i < a.size() ? a[i].toInt() : 0; };

    if (op == QLatin1String("clear")) {
        m_canvas.fill(a.isEmpty() ? QColor(Qt::black) : color(a[0]));
    } else if (op == QLatin1String("rect")) {
        // color linewidth x y w h   (filled when linewidth == 0)
        const QColor c = color(a.value(0));
        const int lw = n(1), x = n(2), y = n(3), w = n(4), h = n(5);
        if (lw <= 0) {
            p.fillRect(x, y, w, h, c);
        } else {
            p.setPen(QPen(c, lw));
            p.drawRect(x, y, w, h);
        }
    } else if (op == QLatin1String("line")) {
        p.setPen(QPen(color(a.value(0)), std::max(1, n(1))));
        p.drawLine(n(2), n(3), n(4), n(5));
    } else if (op == QLatin1String("dot")) {
        const QColor c = color(a.value(0));
        const int sz = std::max(1, n(1));
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(n(2), n(3), sz, sz);
    } else if (op == QLatin1String("fill")) {
        // Approximate flood fill with a rectangle fill at the point's region.
        p.fillRect(m_canvas.rect(), color(a.value(0)));
    } else if (op == QLatin1String("text")) {
        // color font size x y text...
        const QColor c = color(a.value(0));
        QFont f(a.value(1).isEmpty() ? QStringLiteral("Sans") : a.value(1));
        f.setPixelSize(std::max(6, n(2)));
        p.setFont(f);
        p.setPen(c);
        QString text;
        for (int i = 5; i < a.size(); ++i) text += (i > 5 ? " " : "") + a[i];
        p.drawText(n(3), n(4) + f.pixelSize(), text);
    }
    update();
}

void PictureWindow::resizeEvent(QResizeEvent*) { ensureCanvas(size()); }

void PictureWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.drawImage(0, 0, m_canvas);
}

}  // namespace lurker::gui
