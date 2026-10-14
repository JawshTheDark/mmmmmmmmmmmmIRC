// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Pure helpers for editbox tab-completion, mIRC-style: find the word under the
// cursor, filter candidates by its prefix, and build the replacement with the
// right trailing suffix (": " when the word starts the line, " " otherwise).
// The Qt editbox drives these; the logic stays here so it is unit-tested.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace lurker {

struct WordSpan {
    std::size_t start = 0;  // index of the first character of the word
    std::size_t end = 0;    // one past the last (== cursor)
    bool atLineStart() const { return start == 0; }
    std::size_t length() const { return end - start; }
};

// The word being typed: the run of non-space characters ending at `cursor`.
WordSpan wordBeforeCursor(std::string_view text, std::size_t cursor);

// Candidates whose start matches `prefix` case-insensitively (ASCII fold),
// keeping the input order. An empty prefix returns all candidates.
std::vector<std::string> filterByPrefix(const std::vector<std::string>& candidates, std::string_view prefix);

// The text that replaces the word: candidate + suffix. `atLineStart` picks
// which suffix (mIRC uses ": " at the start of the line, " " elsewhere).
std::string completionReplacement(std::string_view candidate, bool atLineStart, std::string_view startSuffix = ": ",
                                  std::string_view midSuffix = " ");

// Convenience: given the current text and cursor and an ordered candidate pool,
// return the list of fully-formed replacement lines to cycle through (each with
// its new cursor position just after the inserted suffix). Empty when nothing
// matches. `outWord` receives the span that was replaced.
struct Completion {
    std::string text;       // the whole new line
    std::size_t cursor = 0; // caret position after completion
};
std::vector<Completion> buildCompletions(std::string_view text, std::size_t cursor,
                                         const std::vector<std::string>& candidates,
                                         std::string_view startSuffix = ": ", std::string_view midSuffix = " ");

}  // namespace lurker
