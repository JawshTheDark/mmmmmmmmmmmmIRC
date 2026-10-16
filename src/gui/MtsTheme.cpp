// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/MtsTheme.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPalette>
#include <QSettings>
#include <QStringList>
#include <QStyleFactory>
#include <QTextStream>

#include "gui/Theme.h"

namespace lurker::gui {

namespace {
// mIRC's COLORS line maps UI slots to palette indices; slot 0 is the window
// background and slot 1 the normal text (verified against the Anova reference:
// 15=light-grey bg, 1=black text).
constexpr int kSlotBackground = 0;
constexpr int kSlotText = 1;

// The currently-applied theme (valid=false when none). Event rendering reads it.
MtsTheme g_active;
}  // namespace

const MtsTheme* activeMtsTheme() { return g_active.valid ? &g_active : nullptr; }

MtsTheme MtsTheme::parse(const QString& path) {
    MtsTheme t;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return t;
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    std::vector<int> colorSlots;  // COLORS: UI-slot -> palette index
    while (!in.atEnd()) {
        QString line = in.readLine();
        if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);  // strip BOM
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('['))) continue;  // [mts] header
        const int sp = line.indexOf(QLatin1Char(' '));
        const QString key = (sp < 0 ? line : line.left(sp));
        const QString val = (sp < 0 ? QString() : line.mid(sp + 1));
        t.directives.insert(key.toUpper(), val);

        const QString up = key.toUpper();
        if (up == QLatin1String("NAME")) t.name = val;
        else if (up == QLatin1String("AUTHOR")) t.author = val;
        else if (up == QLatin1String("SCRIPT")) t.script = val;
        else if (up == QLatin1String("RGBCOLORS")) {
            const QStringList triples = val.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            for (int i = 0; i < 16 && i < triples.size(); ++i) {
                const QStringList rgb = triples[i].split(QLatin1Char(','));
                if (rgb.size() == 3)
                    t.palette[static_cast<std::size_t>(i)] = {
                        static_cast<unsigned char>(rgb[0].toInt()),
                        static_cast<unsigned char>(rgb[1].toInt()),
                        static_cast<unsigned char>(rgb[2].toInt())};
            }
            t.hasPalette = triples.size() >= 16;
        } else if (up == QLatin1String("COLORS")) {
            for (const QString& n : val.split(QLatin1Char(','), Qt::SkipEmptyParts))
                colorSlots.push_back(n.trimmed().toInt());
        } else if (up == QLatin1String("FONTDEFAULT")) {
            // "Family,Size"
            const int comma = val.lastIndexOf(QLatin1Char(','));
            t.fontFamily = (comma < 0 ? val : val.left(comma)).trimmed();
            if (comma >= 0) t.fontSize = val.mid(comma + 1).trimmed().toInt();
        }
    }

    // Derive background/text from COLORS + the palette.
    auto slotColor = [&](int slot) -> QColor {
        if (slot < 0 || slot >= static_cast<int>(colorSlots.size())) return QColor();
        const int idx = colorSlots[static_cast<std::size_t>(slot)];
        if (idx < 0 || idx > 15) return QColor();
        const auto& p = t.hasPalette ? t.palette : std::array<lurker::Rgb, 16>{};
        if (!t.hasPalette) return QColor();
        return QColor(p[static_cast<std::size_t>(idx)].r, p[static_cast<std::size_t>(idx)].g,
                      p[static_cast<std::size_t>(idx)].b);
    };
    t.background = slotColor(kSlotBackground);
    t.text = slotColor(kSlotText);

    t.valid = !t.name.isEmpty() || t.hasPalette || !t.directives.isEmpty();
    return t;
}

bool MtsTheme::apply() const {
    if (!valid) return false;
    if (hasPalette) lurker::setPalette(palette);  // \x03 codes render in theme colours
    QSettings s;
    if (background.isValid()) s.setValue(QStringLiteral("display/bgColor"), background.name());
    if (text.isValid()) s.setValue(QStringLiteral("colors/event/message"), text.name());
    if (text.isValid()) s.setValue(QStringLiteral("display/nicklistColor"), text.name());
    if (background.isValid()) s.setValue(QStringLiteral("display/nicklistBgColor"), background.name());
    if (!fontFamily.isEmpty()) s.setValue(QStringLiteral("display/fontFamily"), fontFamily);
    if (fontSize > 0) s.setValue(QStringLiteral("display/fontSize"), fontSize);
    s.setValue(QStringLiteral("mts/loaded"), name.isEmpty() ? QStringLiteral("theme") : name);

    // Theme the whole client (tree/nicklist/previews/chrome), not just the
    // message area: an app-wide palette plus the item-view stylesheet the item
    // views need to actually repaint on Windows — the same treatment the built-in
    // themes use. "Clear MTS theme" restores the app theme.
    if (background.isValid() && text.isValid()) {
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        const bool dark = background.lightness() < 128;
        const QColor accent = hasPalette ? QColor(palette[12].r, palette[12].g, palette[12].b)
                                         : QColor(0x3D, 0x7E, 0xFF);
        QPalette p;
        p.setColor(QPalette::Window, background);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::Base, background);
        p.setColor(QPalette::AlternateBase, dark ? background.lighter(130) : background.darker(105));
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, dark ? background.lighter(120) : background.darker(103));
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::Highlight, accent);
        p.setColor(QPalette::HighlightedText, dark ? text : QColor(Qt::white));
        p.setColor(QPalette::Link, accent);
        p.setColor(QPalette::ToolTipBase, background);
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Disabled, QPalette::Text, text.darker(dark ? 170 : 130));
        qApp->setPalette(p);
        qApp->setStyleSheet(QStringLiteral("QTreeView,QTreeWidget,QListView,QListWidget{background:%1;color:%2;}"
                                           "QHeaderView::section{background:%1;color:%2;}"
                                           "QDockWidget{color:%2;}QDockWidget::title{background:%1;}")
                                .arg(background.name(), text.name()));
    }
    g_active = *this;  // becomes the active theme for event rendering
    return true;
}

QString MtsTheme::timestamp(const QDateTime& dt) const {
    if (directives.value(QStringLiteral("TIMESTAMP")).compare(QLatin1String("on"), Qt::CaseInsensitive) != 0)
        return QString();
    const QString fmt = directives.value(QStringLiteral("TIMESTAMPFORMAT"));
    if (fmt.isEmpty()) return QString();
    const QTime t = dt.time();
    const QDate d = dt.date();
    auto two = [](int v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); };
    const int h12 = (t.hour() % 12) == 0 ? 12 : t.hour() % 12;
    // mIRC $asctime time/date tokens, matched longest-first; other chars (literals
    // and \x03 colour codes) pass through untouched.
    const std::vector<std::pair<QString, QString>> toks{
        {QStringLiteral("HH"), two(t.hour())},   {QStringLiteral("hh"), two(h12)},
        {QStringLiteral("nn"), two(t.minute())}, {QStringLiteral("ss"), two(t.second())},
        {QStringLiteral("TT"), t.hour() < 12 ? QStringLiteral("AM") : QStringLiteral("PM")},
        {QStringLiteral("tt"), t.hour() < 12 ? QStringLiteral("am") : QStringLiteral("pm")},
        {QStringLiteral("yyyy"), QString::number(d.year())},
        {QStringLiteral("dd"), two(d.day())},
        {QStringLiteral("H"), QString::number(t.hour())}, {QStringLiteral("h"), QString::number(h12)},
        {QStringLiteral("n"), QString::number(t.minute())}, {QStringLiteral("s"), QString::number(t.second())},
        {QStringLiteral("T"), t.hour() < 12 ? QStringLiteral("A") : QStringLiteral("P")},
        {QStringLiteral("t"), t.hour() < 12 ? QStringLiteral("a") : QStringLiteral("p")}};
    QString out;
    for (int i = 0; i < fmt.size();) {
        bool hit = false;
        for (const auto& [tok, val] : toks) {
            if (QStringView(fmt).mid(i).startsWith(tok)) {
                out += val;
                i += tok.size();
                hit = true;
                break;
            }
        }
        if (!hit) out += fmt[i++];
    }
    return out;
}

bool MtsTheme::nicklistDecor(const QString& sigil, QString& glyph, QColor& color) const {
    const QChar s = sigil.isEmpty() ? QChar() : sigil.at(0);
    QString rank;
    if (s == QLatin1Char('~') || s == QLatin1Char('&')) rank = QStringLiteral("OWNER");
    else if (s == QLatin1Char('@')) rank = QStringLiteral("OP");
    else if (s == QLatin1Char('%')) rank = QStringLiteral("HOP");
    else if (s == QLatin1Char('+')) rank = QStringLiteral("VOICE");
    else rank = QStringLiteral("REGULAR");
    const QString clineKey = QStringLiteral("CLINE") + rank;
    const QString charKey = QStringLiteral("CLINECHAR") + rank;
    if (!directives.contains(clineKey) && !directives.contains(charKey)) return false;
    glyph = directives.value(charKey);  // themed prefix glyph (@, +, ., %, …)
    // The CLINE value carries a palette index (e.g. "\x037"); resolve via the
    // theme palette, else leave the colour invalid (caller keeps the default).
    const QString cv = directives.value(clineKey);
    QString digits;
    for (const QChar c : cv) {
        if (c.isDigit()) digits += c;
        else if (!digits.isEmpty()) break;
    }
    if (!digits.isEmpty() && hasPalette) {
        const int idx = digits.toInt();
        if (idx >= 0 && idx < 16)
            color = QColor(palette[static_cast<std::size_t>(idx)].r, palette[static_cast<std::size_t>(idx)].g,
                           palette[static_cast<std::size_t>(idx)].b);
    }
    return true;
}

QString MtsTheme::renderEvent(const QString& key, const QHash<QString, QString>& vars) const {
    const QString tmpl = directives.value(key.toUpper());
    if (tmpl.isEmpty()) return QString();
    if (tmpl.startsWith(QLatin1String("!script"))) return QString();  // companion script: phase 3
    QString out = tmpl;
    out.replace(QLatin1String("<pre>"), directives.value(QStringLiteral("PREFIX")));
    out.replace(QLatin1String("<lt>"), QStringLiteral("<"));
    out.replace(QLatin1String("<gt>"), QStringLiteral(">"));
    for (auto it = vars.constBegin(); it != vars.constEnd(); ++it)
        out.replace(QStringLiteral("<%1>").arg(it.key()), it.value());
    return out;
}

void clearMtsTheme() {
    lurker::resetPalette();
    g_active.valid = false;  // stop routing events through templates
    QSettings s;
    s.remove(QStringLiteral("display/bgColor"));
    s.remove(QStringLiteral("colors/event/message"));
    s.remove(QStringLiteral("display/nicklistColor"));
    s.remove(QStringLiteral("display/nicklistBgColor"));
    s.remove(QStringLiteral("mts/loaded"));
    // Restore the app theme's palette + stylesheet (the MTS palette overrode it).
    applyTheme(*qApp, s.value(QStringLiteral("theme"), QStringLiteral("default")).toString());
    // Font is left as the user set it; MTS only overrode it while active.
}

}  // namespace lurker::gui
