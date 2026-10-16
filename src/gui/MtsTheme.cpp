// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/MtsTheme.h"

#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTextStream>

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
    if (!fontFamily.isEmpty()) s.setValue(QStringLiteral("display/fontFamily"), fontFamily);
    if (fontSize > 0) s.setValue(QStringLiteral("display/fontSize"), fontSize);
    s.setValue(QStringLiteral("mts/loaded"), name.isEmpty() ? QStringLiteral("theme") : name);
    g_active = *this;  // becomes the active theme for event rendering
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
    s.remove(QStringLiteral("mts/loaded"));
    // Font is left as the user set it; MTS only overrode it while active.
}

}  // namespace lurker::gui
