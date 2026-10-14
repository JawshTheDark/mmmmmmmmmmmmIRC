// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "core/IrcMessage.h"

#include <algorithm>
#include <cctype>

namespace lurker {

namespace {

// Splits off the next space-delimited token; runs of spaces count as one separator.
std::string_view nextToken(std::string_view& rest) {
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    const std::size_t end = rest.find(' ');
    const std::string_view token = rest.substr(0, end);
    rest.remove_prefix(end == std::string_view::npos ? rest.size() : end);
    return token;
}

}  // namespace

std::string unescapeTagValue(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        if (i + 1 >= value.size()) break;  // a lone trailing backslash is dropped
        const char e = value[++i];
        switch (e) {
            case ':': out.push_back(';'); break;
            case 's': out.push_back(' '); break;
            case '\\': out.push_back('\\'); break;
            case 'r': out.push_back('\r'); break;
            case 'n': out.push_back('\n'); break;
            default: out.push_back(e); break;  // unknown escapes drop the backslash
        }
    }
    return out;
}

std::string escapeTagValue(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case ';': out += "\\:"; break;
            case ' ': out += "\\s"; break;
            case '\\': out += "\\\\"; break;
            case '\r': out += "\\r"; break;
            case '\n': out += "\\n"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::optional<IrcMessage> IrcMessage::parse(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.remove_suffix(1);

    IrcMessage msg;
    std::string_view rest = line;
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);

    if (!rest.empty() && rest.front() == '@') {
        rest.remove_prefix(1);
        const std::string_view tagBlock = nextToken(rest);
        std::size_t start = 0;
        while (start <= tagBlock.size()) {
            const std::size_t semi = tagBlock.find(';', start);
            const std::string_view item =
                tagBlock.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start);
            if (!item.empty()) {
                const std::size_t eq = item.find('=');
                std::string key(item.substr(0, eq));
                std::string val = eq == std::string_view::npos ? std::string() : unescapeTagValue(item.substr(eq + 1));
                // Later duplicates win, per the message-tags spec.
                auto it = std::find_if(msg.tags.begin(), msg.tags.end(), [&](const auto& t) { return t.first == key; });
                if (it != msg.tags.end()) {
                    it->second = std::move(val);
                } else {
                    msg.tags.emplace_back(std::move(key), std::move(val));
                }
            }
            if (semi == std::string_view::npos) break;
            start = semi + 1;
        }
    }

    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    if (!rest.empty() && rest.front() == ':') {
        rest.remove_prefix(1);
        msg.source = std::string(nextToken(rest));
    }

    const std::string_view command = nextToken(rest);
    if (command.empty()) return std::nullopt;
    msg.command.reserve(command.size());
    for (const char c : command) msg.command.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    while (true) {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.empty()) break;
        if (rest.front() == ':') {
            msg.params.emplace_back(rest.substr(1));
            break;
        }
        msg.params.emplace_back(nextToken(rest));
    }
    return msg;
}

std::string IrcMessage::serialize() const {
    std::string out;
    if (!tags.empty()) {
        out.push_back('@');
        for (std::size_t i = 0; i < tags.size(); ++i) {
            if (i) out.push_back(';');
            out += tags[i].first;
            if (!tags[i].second.empty()) {
                out.push_back('=');
                out += escapeTagValue(tags[i].second);
            }
        }
        out.push_back(' ');
    }
    if (!source.empty()) {
        out.push_back(':');
        out += source;
        out.push_back(' ');
    }
    out += command;
    for (std::size_t i = 0; i < params.size(); ++i) {
        out.push_back(' ');
        const std::string& p = params[i];
        const bool last = i + 1 == params.size();
        if (last && (p.empty() || p.front() == ':' || p.find(' ') != std::string::npos)) out.push_back(':');
        out += p;
    }
    return out;
}

std::optional<std::string> IrcMessage::tag(std::string_view key) const {
    for (const auto& [k, v] : tags) {
        if (k == key) return v;
    }
    return std::nullopt;
}

IrcSource IrcMessage::parsedSource() const {
    IrcSource s;
    const std::string_view src = source;
    const std::size_t bang = src.find('!');
    const std::size_t at = src.find('@', bang == std::string_view::npos ? 0 : bang);
    const std::size_t nickEnd = std::min(bang, at);
    s.nick = std::string(src.substr(0, nickEnd));
    if (bang != std::string_view::npos) {
        s.user = std::string(src.substr(bang + 1, at == std::string_view::npos ? std::string_view::npos : at - bang - 1));
    }
    if (at != std::string_view::npos) s.host = std::string(src.substr(at + 1));
    return s;
}

std::string IrcMessage::param(std::size_t i) const { return i < params.size() ? params[i] : std::string(); }

}  // namespace lurker
