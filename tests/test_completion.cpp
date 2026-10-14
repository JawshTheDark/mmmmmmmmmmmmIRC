// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "check.h"
#include "core/Completion.h"

using namespace lurker;

int main() {
    // Word under the cursor.
    {
        WordSpan w = wordBeforeCursor("hello wor", 9);
        CHECK_EQ(w.start, 6u);
        CHECK_EQ(w.end, 9u);
        CHECK(!w.atLineStart());
        w = wordBeforeCursor("ali", 3);
        CHECK_EQ(w.start, 0u);
        CHECK(w.atLineStart());
        w = wordBeforeCursor("hello ", 6);  // cursor after a space => empty word
        CHECK_EQ(w.length(), 0u);
        w = wordBeforeCursor("hello world", 8);  // cursor inside second word
        CHECK_EQ(w.start, 6u);
        CHECK_EQ(w.end, 8u);
    }

    // Prefix filter, case-insensitive, order preserved.
    {
        std::vector<std::string> pool = {"alice", "Albert", "bob", "alan"};
        auto m = filterByPrefix(pool, "al");
        CHECK_EQ(m.size(), 3u);
        CHECK_EQ(m[0], std::string("alice"));
        CHECK_EQ(m[1], std::string("Albert"));
        CHECK_EQ(m[2], std::string("alan"));
        CHECK_EQ(filterByPrefix(pool, "").size(), 4u);
        CHECK_EQ(filterByPrefix(pool, "zz").size(), 0u);
    }

    // Suffix rule: ": " at line start, " " mid-line.
    {
        CHECK_EQ(completionReplacement("alice", true), std::string("alice: "));
        CHECK_EQ(completionReplacement("alice", false), std::string("alice "));
    }

    // Nick completion at the start of a line cycles with ": ".
    {
        std::vector<std::string> nicks = {"alice", "alan", "bob"};
        auto c = buildCompletions("al", 2, nicks);
        CHECK_EQ(c.size(), 2u);
        CHECK_EQ(c[0].text, std::string("alice: "));
        CHECK_EQ(c[0].cursor, 7u);
        CHECK_EQ(c[1].text, std::string("alan: "));
    }

    // Mid-line completion keeps the rest of the line and uses " ".
    {
        std::vector<std::string> nicks = {"alice"};
        auto c = buildCompletions("hi al and bye", 5, nicks);  // cursor after "al"
        CHECK_EQ(c.size(), 1u);
        CHECK_EQ(c[0].text, std::string("hi alice  and bye"));
        CHECK_EQ(c[0].cursor, 9u);  // just after "alice "
    }

    // Command completion: '/' is part of the word, so it matches command names
    // supplied with their leading slash.
    {
        std::vector<std::string> commands = {"/join", "/j", "/part", "/me"};
        auto c = buildCompletions("/j", 2, commands);
        CHECK_EQ(c.size(), 2u);
        CHECK_EQ(c[0].text, std::string("/join: "));  // at line start → ": " (caller may override)
    }

    // A command completer that wants a plain space suffix can pass midSuffix for both.
    {
        std::vector<std::string> commands = {"/join"};
        auto c = buildCompletions("/j", 2, commands, " ", " ");
        CHECK_EQ(c[0].text, std::string("/join "));
    }

    // No candidates => no completions.
    CHECK(buildCompletions("xyz", 3, {"alice"}).empty());

    return check::summary();
}
