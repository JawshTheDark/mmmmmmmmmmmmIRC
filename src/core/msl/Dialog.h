// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Parsed form of an MSL `dialog` table. The engine parses the table (Qt-free)
// and the GUI builds a real dialog from it, so dialog layout stays testable and
// the renderer stays dumb. Geometry is in mIRC dialog units (see the GUI for
// the unit→pixel mapping). Written from mIRC's /dialog documentation.
#pragma once

#include <string>
#include <vector>

namespace lurker::msl {

struct DialogControl {
    std::string type;   // text, edit, button, check, radio, box, list, combo, icon, link, ...
    std::string caption;
    int id = 0;
    int x = 0, y = 0, w = 0, h = 0;      // dialog units
    std::vector<std::string> styles;     // ok, cancel, multi, read, pass, right, ...
    bool hasStyle(const std::string& s) const;
};

struct DialogTable {
    std::string name;
    std::string title;
    int w = 200, h = 150;                // dialog units
    std::vector<DialogControl> controls;
    const DialogControl* findId(int id) const;
};

// Parse a `dialog` body (the text inside the braces) into a table.
DialogTable parseDialogTable(const std::string& name, const std::string& body);

}  // namespace lurker::msl
