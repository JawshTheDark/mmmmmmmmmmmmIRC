// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Parses and applies an MTS (mIRC Theme System) theme file (.mts). MTS is the
// old community theme standard: a flat key/value file describing a 16-colour
// RGBCOLORS palette, a COLORS slot map (which palette entry each UI element
// uses), fonts, per-event line templates, background images and nicklist
// colours. Phase 1 applies the parts that reshape the chat buffer — the colour
// palette (so \x03 codes match the theme), the message background/text colour
// and the font — mapping them onto our existing display settings. The remaining
// directives (event templates, images, nicklist colours, numerics) are parsed
// and kept for later phases. See issue #27.
#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <array>

#include "core/IrcFormat.h"

namespace lurker::gui {

struct MtsTheme {
    bool valid = false;
    QString name;                       // NAME
    QString author;                     // AUTHOR
    QString script;                     // companion SCRIPT (.mrc) to /load, if any
    std::array<lurker::Rgb, 16> palette;  // RGBCOLORS (the 16-colour table)
    QColor background;                  // derived from COLORS + palette
    QColor text;                        // derived from COLORS + palette
    QString fontFamily;                 // FONTDEFAULT family
    int fontSize = 0;                   // FONTDEFAULT size (0 = unset)
    bool hasPalette = false;
    QHash<QString, QString> directives;  // every raw directive, for later phases

    // Parse `path`; a theme with valid=false could not be read.
    static MtsTheme parse(const QString& path);
    // Write this theme onto our display settings and install its colour palette.
    // Caller refreshes open views afterwards. Returns false if not valid.
    bool apply() const;
};

// Restore the built-in palette and clear the MTS display overrides (revert to
// the plain theme). Caller refreshes views.
void clearMtsTheme();

}  // namespace lurker::gui
