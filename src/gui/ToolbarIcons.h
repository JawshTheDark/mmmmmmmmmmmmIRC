// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Clean-room toolbar glyphs. mIRC's toolbar is iconic, so we offer the same
// set of buttons in the same order — but every glyph here is drawn from
// scratch with QPainter (no mIRC artwork is used or copied). Icons paint in
// the active palette so they read correctly on every shipped theme.
#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QRectF>

#include <cmath>

namespace lurker::gui {

inline constexpr double kPi = 3.14159265358979323846;

enum class Glyph {
    Connect,
    Disconnect,
    Join,
    ChannelList,
    Options,
    Colors,
    ScriptEditor,
    LoadScript,
    AddressBook,
    Notify,
    Logs,
    DccSend,
    DccChat,
    Find,
    About,
};

// Draws one 32x32 glyph. `fg` is the line colour (palette text); `accent` is a
// single highlight colour used sparingly, mIRC-style.
inline QIcon makeGlyph(Glyph g, const QColor& fg, const QColor& accent) {
    const int S = 32;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(fg, 2.0);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (g) {
        case Glyph::Connect: {
            // Two plugs meeting — a connection being made.
            QPen a(accent, 2.0);
            a.setCapStyle(Qt::RoundCap);
            p.setPen(a);
            p.drawLine(6, 16, 13, 16);
            p.drawLine(9, 11, 9, 21);
            p.setPen(pen);
            p.drawLine(19, 16, 26, 16);
            p.drawLine(23, 11, 23, 21);
            p.drawLine(13, 16, 15, 16);
            p.drawLine(17, 16, 19, 16);
            break;
        }
        case Glyph::Disconnect: {
            QPen a(accent, 2.0);
            a.setCapStyle(Qt::RoundCap);
            p.setPen(a);
            p.drawLine(6, 16, 12, 16);
            p.drawLine(9, 11, 9, 21);
            p.setPen(pen);
            p.drawLine(20, 16, 26, 16);
            p.drawLine(23, 11, 23, 21);
            // break mark
            p.drawLine(14, 22, 18, 10);
            break;
        }
        case Glyph::Join: {
            // A hash — join a channel.
            QPen a(accent, 2.2);
            a.setCapStyle(Qt::RoundCap);
            p.setPen(a);
            p.drawLine(13, 7, 10, 25);
            p.drawLine(22, 7, 19, 25);
            p.drawLine(8, 13, 25, 13);
            p.drawLine(7, 19, 24, 19);
            break;
        }
        case Glyph::ChannelList: {
            for (int i = 0; i < 4; ++i) {
                const int y = 8 + i * 6;
                p.setBrush(accent);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QRectF(7, y - 1.5, 3, 3));
                p.setPen(pen);
                p.drawLine(13, y, 25, y);
            }
            break;
        }
        case Glyph::Options: {
            // A gear.
            p.save();
            p.translate(16, 16);
            QPainterPath path;
            const double rOut = 11, rIn = 7;
            for (int i = 0; i < 8; ++i) {
                const double a0 = i * kPi / 4.0;
                const double a1 = a0 + kPi / 8.0;
                path.moveTo(rIn * std::cos(a0), rIn * std::sin(a0));
                path.lineTo(rOut * std::cos(a0 + 0.12), rOut * std::sin(a0 + 0.12));
                path.lineTo(rOut * std::cos(a1 - 0.12), rOut * std::sin(a1 - 0.12));
                path.lineTo(rIn * std::cos(a1), rIn * std::sin(a1));
            }
            p.drawPath(path);
            p.setBrush(accent);
            p.drawEllipse(QRectF(-3.2, -3.2, 6.4, 6.4));
            p.restore();
            break;
        }
        case Glyph::Colors: {
            // A painter's palette dabbed with colour.
            p.drawEllipse(QRectF(6, 7, 20, 18));
            p.setPen(Qt::NoPen);
            const QColor dabs[] = {QColor(220, 70, 70), QColor(70, 150, 220), QColor(90, 190, 110),
                                   QColor(230, 190, 70)};
            const QPointF at[] = {{12, 12}, {20, 11}, {22, 18}, {13, 20}};
            for (int i = 0; i < 4; ++i) {
                p.setBrush(dabs[i]);
                p.drawEllipse(at[i], 2.3, 2.3);
            }
            break;
        }
        case Glyph::ScriptEditor: {
            // A document with </> on it.
            p.drawRoundedRect(QRectF(8, 6, 16, 20), 2, 2);
            QPen a(accent, 1.8);
            a.setCapStyle(Qt::RoundCap);
            a.setJoinStyle(Qt::RoundJoin);
            p.setPen(a);
            p.drawPolyline(QPolygonF({{14, 13}, {11, 16}, {14, 19}}));
            p.drawPolyline(QPolygonF({{18, 13}, {21, 16}, {18, 19}}));
            break;
        }
        case Glyph::LoadScript: {
            // A folder opening.
            p.drawLine(7, 11, 7, 24);
            p.drawLine(7, 11, 13, 11);
            p.drawLine(13, 11, 15, 14);
            p.drawLine(15, 14, 25, 14);
            p.setBrush(accent.lighter(170));
            QPolygonF body({{7, 24}, {25, 24}, {27, 15}, {9, 15}});
            p.setPen(pen);
            p.drawPolygon(body);
            break;
        }
        case Glyph::AddressBook: {
            // A little book with a person.
            p.drawRoundedRect(QRectF(9, 6, 15, 20), 2, 2);
            p.drawLine(9, 6, 9, 26);
            p.setBrush(accent);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(15, 11, 4, 4));
            QPainterPath body;
            body.moveTo(13, 21);
            body.arcTo(QRectF(13, 15.5, 8, 9), 0, 180);
            p.drawPath(body);
            break;
        }
        case Glyph::Notify: {
            // A bell.
            QPainterPath bell;
            bell.moveTo(10, 22);
            bell.lineTo(22, 22);
            bell.lineTo(20, 15);
            bell.arcTo(QRectF(12, 7, 8, 8), 0, 180);
            bell.lineTo(10, 22);
            p.drawPath(bell);
            p.setBrush(accent);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(14.5, 22, 3, 3));
            break;
        }
        case Glyph::Logs: {
            p.drawRoundedRect(QRectF(8, 6, 16, 20), 2, 2);
            p.setPen(QPen(accent, 1.6, Qt::SolidLine, Qt::RoundCap));
            for (int i = 0; i < 4; ++i) p.drawLine(11, 11 + i * 4, 21, 11 + i * 4);
            break;
        }
        case Glyph::DccSend: {
            // Up arrow into a tray.
            QPen a(accent, 2.2);
            a.setCapStyle(Qt::RoundCap);
            a.setJoinStyle(Qt::RoundJoin);
            p.setPen(a);
            p.drawLine(16, 7, 16, 18);
            p.drawPolyline(QPolygonF({{11, 12}, {16, 7}, {21, 12}}));
            p.setPen(pen);
            p.drawPolyline(QPolygonF({{7, 20}, {7, 25}, {25, 25}, {25, 20}}));
            break;
        }
        case Glyph::DccChat: {
            // Two speech bubbles.
            p.drawRoundedRect(QRectF(6, 8, 14, 10), 2, 2);
            p.drawLine(10, 18, 10, 21);
            p.drawLine(10, 21, 13, 18);
            QPen a(accent, 2.0);
            a.setJoinStyle(Qt::RoundJoin);
            p.setPen(a);
            p.drawRoundedRect(QRectF(16, 13, 11, 8), 2, 2);
            break;
        }
        case Glyph::Find: {
            p.drawEllipse(QRectF(8, 8, 12, 12));
            p.drawLine(19, 19, 25, 25);
            break;
        }
        case Glyph::About: {
            p.drawEllipse(QRectF(7, 7, 18, 18));
            QPen a(accent, 2.4);
            a.setCapStyle(Qt::RoundCap);
            p.setPen(a);
            p.drawPoint(16, 12);
            p.setPen(pen);
            p.drawLine(16, 15, 16, 22);
            break;
        }
    }
    p.end();
    return QIcon(pm);
}

}  // namespace lurker::gui
