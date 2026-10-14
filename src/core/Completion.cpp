// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/Completion.h"

#include <algorithm>
#include <cctype>

namespace lurker {

namespace {
char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

bool startsWithFold(std::string_view s, std::string_view prefix) {
    if (prefix.size() > s.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lowerAscii(s[i]) != lowerAscii(prefix[i])) return false;
    }
    return true;
}
}  // namespace

WordSpan wordBeforeCursor(std::string_view text, std::size_t cursor) {
    cursor = std::min(cursor, text.size());
    std::size_t start = cursor;
    while (start > 0 && text[start - 1] != ' ') --start;
    return {start, cursor};
}

std::vector<std::string> filterByPrefix(const std::vector<std::string>& candidates, std::string_view prefix) {
    std::vector<std::string> out;
    for (const std::string& c : candidates) {
        if (prefix.empty() || startsWithFold(c, prefix)) out.push_back(c);
    }
    return out;
}

std::string completionReplacement(std::string_view candidate, bool atLineStart, std::string_view startSuffix,
                                  std::string_view midSuffix) {
    std::string out(candidate);
    out += atLineStart ? startSuffix : midSuffix;
    return out;
}

std::vector<Completion> buildCompletions(std::string_view text, std::size_t cursor,
                                         const std::vector<std::string>& candidates, std::string_view startSuffix,
                                         std::string_view midSuffix) {
    const WordSpan word = wordBeforeCursor(text, cursor);
    const std::string_view prefix = text.substr(word.start, word.length());
    std::vector<Completion> out;
    for (const std::string& cand : filterByPrefix(candidates, prefix)) {
        const std::string replacement = completionReplacement(cand, word.atLineStart(), startSuffix, midSuffix);
        std::string line(text.substr(0, word.start));
        line += replacement;
        const std::size_t caret = line.size();
        line += text.substr(word.end);
        out.push_back({std::move(line), caret});
    }
    return out;
}

}  // namespace lurker
