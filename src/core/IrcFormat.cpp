// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/IrcFormat.h"

#include <array>

namespace lurker {

namespace {

constexpr char kBold = '\x02';
constexpr char kColor = '\x03';
constexpr char kHexColor = '\x04';
constexpr char kMono = '\x11';
constexpr char kReverse = '\x16';
constexpr char kReset = '\x0F';
constexpr char kItalic = '\x1D';
constexpr char kStrike = '\x1E';
constexpr char kUnderline = '\x1F';

bool isDigit(char c) { return c >= '0' && c <= '9'; }

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Reads up to two decimal digits at `pos`. Returns -1 (and leaves `pos`) when none.
int readPaletteIndex(std::string_view s, std::size_t& pos) {
    if (pos >= s.size() || !isDigit(s[pos])) return -1;
    int value = s[pos++] - '0';
    if (pos < s.size() && isDigit(s[pos])) value = value * 10 + (s[pos++] - '0');
    return value;
}

// Reads exactly six hex digits at `pos`. Returns nullopt (and leaves `pos`) otherwise.
std::optional<Rgb> readHex(std::string_view s, std::size_t& pos) {
    if (pos + 6 > s.size()) return std::nullopt;
    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = hexValue(s[pos + i]);
        if (v[i] < 0) return std::nullopt;
    }
    pos += 6;
    return Rgb{static_cast<std::uint8_t>(v[0] * 16 + v[1]), static_cast<std::uint8_t>(v[2] * 16 + v[3]),
               static_cast<std::uint8_t>(v[4] * 16 + v[5])};
}

Color paletteToColor(int index) { return index == 99 ? Color{} : Color::palette(index); }

}  // namespace

std::vector<Span> parseFormatting(std::string_view s) {
    std::vector<Span> spans;
    Style style;
    std::string run;

    auto flush = [&] {
        if (run.empty()) return;
        if (!spans.empty() && spans.back().style == style) {
            spans.back().text += run;
        } else {
            spans.push_back({run, style});
        }
        run.clear();
    };

    for (std::size_t i = 0; i < s.size();) {
        const char c = s[i];
        switch (c) {
            case kBold:      flush(); style.bold = !style.bold; ++i; break;
            case kItalic:    flush(); style.italic = !style.italic; ++i; break;
            case kUnderline: flush(); style.underline = !style.underline; ++i; break;
            case kStrike:    flush(); style.strike = !style.strike; ++i; break;
            case kMono:      flush(); style.mono = !style.mono; ++i; break;
            case kReverse:   flush(); style.reverse = !style.reverse; ++i; break;
            case kReset:     flush(); style = Style{}; ++i; break;

            case kColor: {
                flush();
                ++i;
                const int fg = readPaletteIndex(s, i);
                if (fg < 0) {
                    // A bare \x03 clears both colours.
                    style.fg = Color{};
                    style.bg = Color{};
                    break;
                }
                style.fg = paletteToColor(fg);
                // ",<digits>" sets the background; a comma not followed by a digit is text.
                if (i + 1 < s.size() && s[i] == ',' && isDigit(s[i + 1])) {
                    ++i;
                    style.bg = paletteToColor(readPaletteIndex(s, i));
                }
                break;
            }

            case kHexColor: {
                flush();
                ++i;
                const auto fg = readHex(s, i);
                if (!fg) {
                    style.fg = Color{};
                    style.bg = Color{};
                    break;
                }
                style.fg = Color::fromRgb(*fg);
                if (i < s.size() && s[i] == ',') {
                    std::size_t probe = i + 1;
                    if (const auto bg = readHex(s, probe)) {
                        style.bg = Color::fromRgb(*bg);
                        i = probe;
                    }
                }
                break;
            }

            default:
                run.push_back(c);
                ++i;
                break;
        }
    }
    flush();
    return spans;
}

std::string stripFormatting(std::string_view input) {
    std::string out;
    for (const auto& span : parseFormatting(input)) out += span.text;
    return out;
}

std::optional<Rgb> paletteColor(int index) {
    static constexpr std::array<Rgb, 16> kPalette{{
        {0xFF, 0xFF, 0xFF},  //  0 white
        {0x00, 0x00, 0x00},  //  1 black
        {0x00, 0x00, 0x7F},  //  2 navy
        {0x00, 0x93, 0x00},  //  3 green
        {0xFF, 0x00, 0x00},  //  4 red
        {0x7F, 0x00, 0x00},  //  5 maroon
        {0x9C, 0x00, 0x9C},  //  6 purple
        {0xFC, 0x7F, 0x00},  //  7 orange
        {0xFF, 0xFF, 0x00},  //  8 yellow
        {0x00, 0xFC, 0x00},  //  9 lime
        {0x00, 0x93, 0x93},  // 10 teal
        {0x00, 0xFF, 0xFF},  // 11 cyan
        {0x00, 0x00, 0xFC},  // 12 blue
        {0xFF, 0x00, 0xFF},  // 13 pink
        {0x7F, 0x7F, 0x7F},  // 14 grey
        {0xD2, 0xD2, 0xD2},  // 15 light grey
    }};
    if (index < 0 || index >= static_cast<int>(kPalette.size())) return std::nullopt;
    return kPalette[static_cast<std::size_t>(index)];
}

}  // namespace lurker
