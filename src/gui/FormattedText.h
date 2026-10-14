// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#pragma once

#include <QColor>
#include <QString>
#include <QTextCursor>

namespace lurker::gui {

// Default colours a formatted run falls back to (the window's own scheme).
struct TextScheme {
    QColor foreground;
    QColor background;
};

// Appends `utf8` at `cursor`, translating IRC formatting codes into character formats.
void insertFormatted(QTextCursor& cursor, const QByteArray& utf8, const TextScheme& scheme);

}  // namespace lurker::gui
