// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/FormattedText.h"

#include <QFontDatabase>
#include <QSettings>
#include <QTextCharFormat>

#include <array>

#include "core/IrcFormat.h"

namespace lurker::gui {

namespace {

// The user's overrides for the 16 mIRC palette slots (Options ▸ Display ▸
// Colors). An invalid entry means "use the built-in default for that slot".
// Cached so bulk history rendering doesn't hit QSettings per line.
std::array<QColor, 16> g_userPalette;
bool g_paletteLoaded = false;

void loadUserPalette() {
    QSettings s;
    for (int i = 0; i < 16; ++i) {
        const QString key = QStringLiteral("colors/%1").arg(i);
        g_userPalette[i] = s.contains(key) ? QColor(s.value(key).toString()) : QColor();
    }
    g_paletteLoaded = true;
}

const std::array<QColor, 16>& userPalette() {
    if (!g_paletteLoaded) loadUserPalette();
    return g_userPalette;
}

// Inserts `text` under `base`, turning http(s):// runs into clickable anchors.
void insertRunWithLinks(QTextCursor& cursor, const QString& text, const QTextCharFormat& base, const QColor& linkColor) {
    int i = 0;
    const int n = text.size();
    while (i < n) {
        int http = text.indexOf(QStringLiteral("http://"), i, Qt::CaseInsensitive);
        int https = text.indexOf(QStringLiteral("https://"), i, Qt::CaseInsensitive);
        int start = (http < 0) ? https : (https < 0 ? http : qMin(http, https));
        if (start < 0) {
            cursor.insertText(text.mid(i), base);
            return;
        }
        if (start > i) cursor.insertText(text.mid(i, start - i), base);
        int end = start;
        while (end < n && !text.at(end).isSpace()) ++end;
        // Don't swallow trailing sentence punctuation or a closing bracket.
        while (end > start && QStringLiteral(".,;:!?)]}>\"'").contains(text.at(end - 1))) --end;
        const QString url = text.mid(start, end - start);
        QTextCharFormat link = base;
        link.setAnchor(true);
        link.setAnchorHref(url);
        link.setForeground(linkColor);
        link.setFontUnderline(true);
        cursor.insertText(url, link);
        i = end;
    }
}

QColor resolve(const Color& c, const QColor& fallback) {
    switch (c.kind) {
        case Color::Kind::Default:
            return fallback;
        case Color::Kind::Rgb:
            return QColor(c.rgb.r, c.rgb.g, c.rgb.b);
        case Color::Kind::Palette: {
            if (c.index >= 0 && c.index < 16 && userPalette()[c.index].isValid()) return userPalette()[c.index];
            if (const auto rgb = paletteColor(c.index)) return QColor(rgb->r, rgb->g, rgb->b);
            return fallback;
        }
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
        const QString runText = QString::fromUtf8(span.text.data(), static_cast<qsizetype>(span.text.size()));
        insertRunWithLinks(cursor, runText, fmt, scheme.foreground.lightness() > 128 ? QColor(0x6c, 0xa0, 0xff)
                                                                                     : QColor(0x1a, 0x5f, 0xb4));
    }
}

void refreshColorSettings() { loadUserPalette(); }

QString stripFormattingQt(const QByteArray& utf8) {
    const std::string stripped =
        stripFormatting(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
    return QString::fromUtf8(stripped.data(), static_cast<qsizetype>(stripped.size()));
}

}  // namespace lurker::gui
