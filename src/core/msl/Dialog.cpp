// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/msl/Dialog.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace lurker::msl {

namespace {
std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string unquote(const std::string& s) {
    std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') return t.substr(1, t.size() - 2);
    return t;
}

// Split on commas that are not inside double quotes.
std::vector<std::string> splitCommas(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inq = false;
    for (char c : s) {
        if (c == '"') inq = !inq;
        if (c == ',' && !inq) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string w;
    while (is >> w) out.push_back(w);
    return out;
}

int toInt(const std::string& s, int def = 0) {
    try {
        return std::stoi(trim(s));
    } catch (...) {
        return def;
    }
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
}  // namespace

bool DialogControl::hasStyle(const std::string& s) const {
    return std::find(styles.begin(), styles.end(), s) != styles.end();
}

const DialogControl* DialogTable::findId(int id) const {
    for (const auto& c : controls) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

DialogTable parseDialogTable(const std::string& name, const std::string& body) {
    DialogTable table;
    table.name = name;
    std::istringstream in(body);
    std::string line;
    static const std::vector<std::string> kTypes = {"text", "edit",   "button", "check", "radio", "box",
                                                    "list", "combo",  "icon",   "link",  "scroll", "menu",
                                                    "item", "tab",    "image"};
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';') continue;
        const std::string kw = lower(words(t).empty() ? std::string() : words(t)[0]);
        if (kw == "title") {
            table.title = unquote(trim(t.substr(5)));
        } else if (kw == "size") {
            const auto nums = words(trim(t.substr(4)));
            if (nums.size() >= 4) {
                table.w = toInt(nums[2]);
                table.h = toInt(nums[3]);
            }
        } else if (kw == "option") {
            // option dbu/pixels/... — noted, we render in dialog units by default
        } else if (std::find(kTypes.begin(), kTypes.end(), kw) != kTypes.end()) {
            // Two mIRC control grammars:
            //   captioned:    <type> "caption", id, x y w h, styles...   (text/edit/button/check/radio/box/link)
            //   caption-less: <type> id, x y w h, styles...              (list/combo/icon/scroll — id comes first)
            // Distinguish by the first field: a bare integer means caption-less.
            DialogControl c;
            c.type = kw;
            const std::string rest = trim(t.substr(kw.size()));
            const auto fields = splitCommas(rest);
            const std::string f0 = fields.empty() ? std::string() : trim(fields[0]);
            const bool idFirst = !f0.empty() && f0.front() != '"' &&
                                 std::all_of(f0.begin(), f0.end(), [](unsigned char ch) { return std::isdigit(ch); });
            std::size_t idF = idFirst ? 0 : 1, geomF = idFirst ? 1 : 2, styleF = idFirst ? 2 : 3;
            if (!idFirst && !fields.empty()) c.caption = unquote(fields[0]);
            if (fields.size() > idF) c.id = toInt(fields[idF]);
            if (fields.size() > geomF) {
                const auto g = words(fields[geomF]);
                if (g.size() >= 4) {
                    c.x = toInt(g[0]);
                    c.y = toInt(g[1]);
                    c.w = toInt(g[2]);
                    c.h = toInt(g[3]);
                }
            }
            for (std::size_t i = styleF; i < fields.size(); ++i) {
                for (const auto& w : words(lower(fields[i]))) c.styles.push_back(w);
            }
            table.controls.push_back(std::move(c));
        }
    }
    return table;
}

}  // namespace lurker::msl
