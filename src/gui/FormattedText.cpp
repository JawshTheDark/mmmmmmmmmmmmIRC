// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/FormattedText.h"

#include <QFontDatabase>
#include <QTextCharFormat>

#include "core/IrcFormat.h"

namespace lurker::gui {

namespace {

QColor resolve(const Color& c, const QColor& fallback) {
    switch (c.kind) {
        case Color::Kind::Default:
            return fallback;
        case Color::Kind::Rgb:
            return QColor(c.rgb.r, c.rgb.g, c.rgb.b);
        case Color::Kind::Palette:
            if (const auto rgb = paletteColor(c.index)) return QColor(rgb->r, rgb->g, rgb->b);
            return fallback;
    }
    return fallback;
}

}  // namespace

void insertFormatted(QTextCursor& cursor, const QByteArray& utf8, const TextScheme& scheme) {
    const auto spans = parseFormatting(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    for (const Span& span : spans) {
        QTextCharFormat fmt;
        const Style& s = span.style;
        QColor fg = resolve(s.fg, scheme.foreground);
        QColor bg = resolve(s.bg, scheme.background);
        if (s.reverse) std::swap(fg, bg);
        fmt.setForeground(fg);
        if (s.bg.kind != Color::Kind::Default || s.reverse) fmt.setBackground(bg);
        if (s.bold) fmt.setFontWeight(QFont::Bold);
        fmt.setFontItalic(s.italic);
        fmt.setFontUnderline(s.underline);
        fmt.setFontStrikeOut(s.strike);
        if (s.mono) fmt.setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        cursor.insertText(QString::fromUtf8(span.text.data(), static_cast<qsizetype>(span.text.size())), fmt);
    }
}

QString stripFormattingQt(const QByteArray& utf8) {
    const std::string stripped =
        stripFormatting(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    return QString::fromUtf8(stripped.data(), static_cast<qsizetype>(stripped.size()));
}

}  // namespace lurker::gui
