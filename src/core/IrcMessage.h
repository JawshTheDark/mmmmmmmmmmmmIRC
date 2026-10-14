// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// One IRC protocol line: IRCv3 message tags, source prefix, command and
// parameters. Written from the IRC client protocol and IRCv3 message-tags
// specifications (https://modern.ircdocs.horse, https://ircv3.net/specs/extensions/message-tags).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lurker {

struct IrcSource {
    std::string nick;  // or server name when there is no '!' / '@'
    std::string user;
    std::string host;
    bool operator==(const IrcSource&) const = default;
};

struct IrcMessage {
    // Tags in wire order. A tag with no value ("@+typing" or "@a=") has an empty value.
    std::vector<std::pair<std::string, std::string>> tags;
    std::string source;  // raw prefix without the leading ':'; empty when absent
    std::string command; // upper-cased; numerics stay as three digits
    std::vector<std::string> params;

    // Parses one line. Trailing "\r\n" is accepted. Returns nullopt for a line
    // with no command.
    static std::optional<IrcMessage> parse(std::string_view line);

    // Serializes without the "\r\n" terminator.
    std::string serialize() const;

    std::optional<std::string> tag(std::string_view key) const;
    IrcSource parsedSource() const;
    // Parameter `i`, or empty when absent.
    std::string param(std::size_t i) const;

    bool operator==(const IrcMessage&) const = default;
};

std::string unescapeTagValue(std::string_view value);
std::string escapeTagValue(std::string_view value);

}  // namespace lurker
