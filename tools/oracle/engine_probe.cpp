// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// engine-probe — evaluate an oracle cases file with THIS repo's MSL engine and
// write "<lineNo><TAB><result>" per case, in the exact format real mIRC's
// probe.mrc produces (see tools/oracle/). Diffing the two outputs is the
// differential test: every difference is a bug to fix or a documented,
// intentional difference. Qt-free: links only lurker_core, so it runs anywhere
// the engine tests do.
//
// Usage: engine-probe <cases-file> <results-file>
//   - a blank line or one starting with ';' is a comment: skipped (no output)
//   - a line starting with '/' is a command case: skipped here (expression-only)
//   - any other line is evaluated and written as "<lineNo>\t<result>"

#include <fstream>
#include <iostream>
#include <string>

#include "core/msl/Host.h"
#include "core/msl/Interpreter.h"

namespace {
using namespace lurker::msl;

// A do-nothing host: expression evaluation needs no IRC state, windows, timers
// or files. unixTime is fixed so any time-based case is deterministic.
class ProbeHost : public Host {
public:
    void echo(const ScriptTarget&, const std::string&) override {}
    void sendMessage(int, const std::string&, const std::string&) override {}
    void sendAction(int, const std::string&, const std::string&) override {}
    void sendRaw(int, const std::string&) override {}
    ScriptTarget activeTarget() const override { return {}; }
    std::string me(int) const override { return "me"; }
    long long unixTime() const override { return 0; }
    std::string startTimer(const std::string&, int, double, const std::string&) override { return {}; }
    void stopTimer(const std::string&) override {}
    void scriptError(const std::string&) override {}
};
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: engine-probe <cases-file> <results-file>\n";
        return 2;
    }
    std::ifstream in(argv[1]);
    std::ofstream out(argv[2]);
    if (!in) {
        std::cerr << "engine-probe: cannot read " << argv[1] << '\n';
        return 2;
    }
    if (!out) {
        std::cerr << "engine-probe: cannot write " << argv[2] << '\n';
        return 2;
    }

    ProbeHost host;
    Interpreter interp(host);

    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
        if (line.empty() || line[0] == ';') continue;  // comment / blank
        if (line[0] == '/') continue;                  // command case: not handled here
        Scope scope;
        out << n << '\t' << interp.evaluate(line, scope) << '\n';
    }
    return 0;
}
