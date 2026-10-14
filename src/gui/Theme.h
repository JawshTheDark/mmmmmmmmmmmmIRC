// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Application skins. A skin is a QPalette + a Qt style-sheet applied app-wide.
// Shipped skins are drawn fresh (no copied assets); the BeOS skin is the
// owner's request. The deeper per-window/per-channel theming (backgrounds,
// fonts, text size, import/export) layers on top of this later.
#pragma once

#include <QString>
#include <QStringList>

class QApplication;

namespace lurker::gui {

struct Theme {
    QString id;
    QString name;
};

// All available skins, in menu order.
QList<Theme> availableThemes();

// Apply a skin by id ("default", "dark", "oled", "beos") to the whole app.
void applyTheme(QApplication& app, const QString& id);

}  // namespace lurker::gui
