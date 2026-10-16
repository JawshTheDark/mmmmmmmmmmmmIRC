// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Parser for IRC text-formatting control codes, written from the public IRC
// formatting specification (https://modern.ircdocs.horse/formatting): bold,
// italic, underline, strikethrough, monospace, reverse, reset, and the
// palette (\x03) and hex (\x04) colour codes.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lurker {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb&) const = default;
};

struct Color {
    enum class Kind { Default, Palette, Rgb };
    Kind kind = Kind::Default;
    int index = 0;  // valid when kind == Palette (0..98; 99 is parsed as Default)
    Rgb rgb;        // valid when kind == Rgb
    bool operator==(const Color&) const = default;

    static Color palette(int i) { return {Kind::Palette, i, {}}; }
    static Color fromRgb(Rgb c) { return {Kind::Rgb, 0, c}; }
};

struct Style {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool mono = false;
    bool reverse = false;
    Color fg;
    Color bg;
    bool operator==(const Style&) const = default;
};

struct Span {
    std::string text;  // UTF-8, control codes removed
    Style style;
    bool operator==(const Span&) const = default;
};

// Splits `input` into runs of uniformly styled text. Adjacent runs with the
// same style are merged and empty runs are dropped.
std::vector<Span> parseFormatting(std::string_view input);

// `input` with every formatting control code (and its colour arguments) removed.
std::string stripFormatting(std::string_view input);

// The active 16-colour palette (indices 0..15). Indices 16..98 (the extended
// palette) are not mapped yet, so they return nullopt and a renderer should
// fall back to the default colour.
std::optional<Rgb> paletteColor(int index);

// Override the 16-colour palette (an MTS theme's RGBCOLORS) or restore the
// built-in one. Affects how \x03 colour codes render everywhere.
void setPalette(const std::array<Rgb, 16>& colors);
void resetPalette();

}  // namespace lurker
