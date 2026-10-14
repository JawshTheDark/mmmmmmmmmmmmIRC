// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "check.h"
#include "core/IrcFormat.h"

using namespace lurker;

int main() {
    // Plain text is a single unstyled span.
    {
        const auto spans = parseFormatting("hello world");
        CHECK_EQ(spans.size(), 1u);
        CHECK_EQ(spans[0].text, std::string("hello world"));
        CHECK(spans[0].style == Style{});
    }

    // Empty input yields no spans.
    CHECK(parseFormatting("").empty());

    // Toggles: bold on, off.
    {
        const auto spans = parseFormatting("a\x02" "b\x02" "c");
        CHECK_EQ(spans.size(), 3u);
        CHECK(!spans[0].style.bold);
        CHECK(spans[1].style.bold);
        CHECK_EQ(spans[1].text, std::string("b"));
        CHECK(!spans[2].style.bold);
    }

    // Every attribute, and reset clears all of them.
    {
        const auto spans = parseFormatting("\x02\x1D\x1F\x1E\x11\x16x\x0Fy");
        CHECK_EQ(spans.size(), 2u);
        const Style& s = spans[0].style;
        CHECK(s.bold && s.italic && s.underline && s.strike && s.mono && s.reverse);
        CHECK(spans[1].style == Style{});
    }

    // Palette colours: foreground only, then foreground and background.
    {
        const auto spans = parseFormatting("\x03" "4red\x03" "4,12 on blue");
        CHECK_EQ(spans.size(), 2u);
        CHECK(spans[0].style.fg == Color::palette(4));
        CHECK(spans[0].style.bg == Color{});
        CHECK_EQ(spans[1].text, std::string(" on blue"));
        CHECK(spans[1].style.fg == Color::palette(4));
        CHECK(spans[1].style.bg == Color::palette(12));
    }

    // Two-digit foreground; the digits after two are plain text.
    {
        const auto spans = parseFormatting("\x03" "041");
        CHECK_EQ(spans.size(), 1u);
        CHECK(spans[0].style.fg == Color::palette(4));
        CHECK_EQ(spans[0].text, std::string("1"));
    }

    // A comma not followed by a digit is literal text.
    {
        const auto spans = parseFormatting("\x03" "4,x");
        CHECK_EQ(spans.size(), 1u);
        CHECK_EQ(spans[0].text, std::string(",x"));
        CHECK(spans[0].style.bg == Color{});
    }

    // A bare \x03 resets both colours; 99 means default.
    {
        // "b" (bare reset) and "c" (colour 99) end up with the same default style, so they merge.
        const auto spans = parseFormatting("\x03" "4,2a\x03" "b\x03" "99c");
        CHECK_EQ(spans.size(), 2u);
        CHECK(spans[0].style.fg == Color::palette(4));
        CHECK(spans[0].style.bg == Color::palette(2));
        CHECK_EQ(spans[1].text, std::string("bc"));
        CHECK(spans[1].style.fg == Color{} && spans[1].style.bg == Color{});
    }

    // Background only is not possible; a new foreground keeps the old background.
    {
        const auto spans = parseFormatting("\x03" "1,2a\x03" "3b");
        CHECK_EQ(spans.size(), 2u);
        CHECK(spans[1].style.fg == Color::palette(3));
        CHECK(spans[1].style.bg == Color::palette(2));
    }

    // Hex colours.
    {
        const auto spans = parseFormatting("\x04" "FF8000,00ff00x\x04y");
        CHECK_EQ(spans.size(), 2u);
        CHECK(spans[0].style.fg == Color::fromRgb({0xFF, 0x80, 0x00}));
        CHECK(spans[0].style.bg == Color::fromRgb({0x00, 0xFF, 0x00}));
        CHECK_EQ(spans[0].text, std::string("x"));
        CHECK(spans[1].style.fg == Color{});
    }

    // Short hex is not a colour: it resets, and the characters stay as text.
    {
        const auto spans = parseFormatting("\x04" "FF80");
        CHECK_EQ(spans.size(), 1u);
        CHECK_EQ(spans[0].text, std::string("FF80"));
        CHECK(spans[0].style.fg == Color{});
    }

    // Adjacent runs with the same style merge (a no-op toggle pair).
    {
        const auto spans = parseFormatting("a\x02\x02" "b");
        CHECK_EQ(spans.size(), 1u);
        CHECK_EQ(spans[0].text, std::string("ab"));
    }

    // Non-ASCII bytes pass through untouched.
    CHECK_EQ(stripFormatting("\x02h\xC3\xA9llo\x0F \xE2\x9C\x93"), std::string("h\xC3\xA9llo \xE2\x9C\x93"));

    // strip removes colour arguments too.
    CHECK_EQ(stripFormatting("\x03" "4,12red on blue\x03 plain"), std::string("red on blue plain"));

    // Palette lookup.
    CHECK(paletteColor(0).has_value());
    CHECK(*paletteColor(4) == (Rgb{0xFF, 0x00, 0x00}));
    CHECK(!paletteColor(16).has_value());
    CHECK(!paletteColor(-1).has_value());

    return check::summary();
}
