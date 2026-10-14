// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// The application icon, drawn from scratch with QPainter (clean-room, no mIRC
// artwork). A rounded badge with a chat-bubble "#" — used for the window/taskbar
// icon and exported to a PNG for packaging (AppImage / Windows).
#pragma once

#include <QColor>
#include <QIcon>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>

namespace lurker::gui {

inline QPixmap appIconPixmap(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double s = size;

    // Rounded badge with a vertical gradient.
    QLinearGradient grad(0, 0, 0, s);
    grad.setColorAt(0.0, QColor(72, 134, 222));
    grad.setColorAt(1.0, QColor(38, 86, 160));
    QPainterPath badge;
    const double m = s * 0.08;
    badge.addRoundedRect(QRectF(m, m, s - 2 * m, s - 2 * m), s * 0.22, s * 0.22);
    p.fillPath(badge, grad);

    // Chat bubble.
    QPainterPath bubble;
    const QRectF br(s * 0.22, s * 0.26, s * 0.56, s * 0.40);
    bubble.addRoundedRect(br, s * 0.09, s * 0.09);
    // little tail
    bubble.moveTo(br.left() + s * 0.14, br.bottom() - s * 0.01);
    bubble.lineTo(br.left() + s * 0.10, br.bottom() + s * 0.10);
    bubble.lineTo(br.left() + s * 0.24, br.bottom() - s * 0.01);
    p.fillPath(bubble, QColor(255, 255, 255, 240));

    // A hash inside the bubble.
    QPen pen(QColor(44, 92, 168), s * 0.045);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const double cx = br.center().x(), cy = br.center().y();
    const double d = s * 0.10, h = s * 0.11;
    p.drawLine(QPointF(cx - d, cy - h), QPointF(cx - d - s * 0.02, cy + h));
    p.drawLine(QPointF(cx + d, cy - h), QPointF(cx + d - s * 0.02, cy + h));
    p.drawLine(QPointF(cx - d - s * 0.06, cy - h * 0.25), QPointF(cx + d + s * 0.02, cy - h * 0.25));
    p.drawLine(QPointF(cx - d - s * 0.08, cy + h * 0.45), QPointF(cx + d, cy + h * 0.45));
    p.end();
    return pm;
}

inline QIcon appIcon() {
    QIcon icon;
    for (int sz : {16, 24, 32, 48, 64, 128, 256}) icon.addPixmap(appIconPixmap(sz));
    return icon;
}

}  // namespace lurker::gui
