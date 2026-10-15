// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <algorithm>
#include <map>
#include <vector>

#include "check.h"
#include "core/msl/Dialog.h"
#include "core/msl/Interpreter.h"

using namespace lurker::msl;

namespace {
struct FakeHost : Host {
    std::vector<std::string> echoed;
    std::vector<std::string> sent;     // "target\ttext"
    std::vector<std::string> actions;  // "target\ttext"
    std::vector<std::string> raws;
    std::vector<std::string> errors;
    ScriptTarget active{1, "#test"};
    long long now = 1000;

    // IRC state for $nick(#,N) / $chan(N) / $comchan / $ison.
    std::vector<std::string> chanNicks;
    std::vector<std::string> myChans;
    std::vector<std::string> channelNicks(int, const std::string&) const override { return chanNicks; }
    std::vector<std::string> myChannels(int) const override { return myChans; }
    bool isOn(int, const std::string&, const std::string& nick) const override {
        return std::find(chanNicks.begin(), chanNicks.end(), nick) != chanNicks.end();
    }

    void echo(const ScriptTarget&, const std::string& t) override { echoed.push_back(t); }
    void sendMessage(int, const std::string& target, const std::string& text) override {
        sent.push_back(target + "\t" + text);
    }
    void sendAction(int, const std::string& target, const std::string& text) override {
        actions.push_back(target + "\t" + text);
    }
    void sendRaw(int, const std::string& line) override { raws.push_back(line); }
    ScriptTarget activeTarget() const override { return active; }
    std::string me(int) const override { return "jawsh"; }
    long long unixTime() const override { return now; }
    std::string appDir() const override { return "/app/"; }
    std::string scriptDir() const override { return "/scripts/"; }
    std::string networkName(int) const override { return "TestNet"; }
    std::string serverHost(int) const override { return "irc.test"; }
    std::vector<std::string> timers;  // "repeat\tinterval\tcommand"
    std::string startTimer(const std::string&, int repeat, double interval, const std::string& cmd) override {
        timers.push_back(std::to_string(repeat) + "\t" + std::to_string(interval) + "\t" + cmd);
        return "timer1";
    }
    void stopTimer(const std::string&) override {}
    void scriptError(const std::string& t) override { errors.push_back(t); }
    std::string lastPrompt;
    std::string inputAnswer = "answer";  // what $?/$input returns
    std::string inputDialog(const std::string& prompt, bool) override {
        lastPrompt = prompt;
        return inputAnswer;
    }

    std::map<std::string, std::string> files;  // in-memory filesystem
    std::string fileRead(const std::string& p) const override {
        auto it = files.find(p);
        return it == files.end() ? std::string() : it->second;
    }
    bool fileWrite(const std::string& p, const std::string& c) override {
        files[p] = c;
        return true;
    }
    bool fileExists(const std::string& p) const override { return files.count(p) != 0; }
    long long fileSize(const std::string& p) const override {
        auto it = files.find(p);
        return it == files.end() ? -1 : static_cast<long long>(it->second.size());
    }
};

std::string eval(Interpreter& in, const std::string& text) {
    Scope s;
    return in.evaluate(text, s);
}
}  // namespace

int main() {
    FakeHost host;
    Interpreter in(host);

    // Plain text passes through.
    CHECK_EQ(eval(in, "hello world"), std::string("hello world"));

    // Identifiers: text/number.
    CHECK_EQ(eval(in, "$len(hello)"), std::string("5"));
    CHECK_EQ(eval(in, "$upper(abc)"), std::string("ABC"));
    CHECK_EQ(eval(in, "$lower(ABC)"), std::string("abc"));
    CHECK_EQ(eval(in, "$left(hello,3)"), std::string("hel"));
    CHECK_EQ(eval(in, "$right(hello,2)"), std::string("lo"));
    CHECK_EQ(eval(in, "$left(hello,-1)"), std::string("hell"));
    CHECK_EQ(eval(in, "$mid(hello,2,3)"), std::string("ell"));
    CHECK_EQ(eval(in, "$mid(hello,3)"), std::string("llo"));
    CHECK_EQ(eval(in, "$str(ab,3)"), std::string("ababab"));
    CHECK_EQ(eval(in, "$chr(65)"), std::string("A"));
    CHECK_EQ(eval(in, "$asc(A)"), std::string("65"));
    CHECK_EQ(eval(in, "$replace(aXbXc,X,-)"), std::string("a-b-c"));
    CHECK_EQ(eval(in, "$remove(hello,l)"), std::string("heo"));
    CHECK_EQ(eval(in, "$pos(hello,ll)"), std::string("3"));
    CHECK_EQ(eval(in, "$pos(banana,a,2)"), std::string("4"));  // Nth occurrence
    CHECK_EQ(eval(in, "$pos(banana,a,0)"), std::string("3"));  // N=0 => count
    CHECK_EQ(eval(in, "$count(banana,a)"), std::string("3"));

    // $calc arithmetic and precedence.
    CHECK_EQ(eval(in, "$calc(2+3*4)"), std::string("14"));
    CHECK_EQ(eval(in, "$calc((2+3)*4)"), std::string("20"));
    CHECK_EQ(eval(in, "$calc(2^10)"), std::string("1024"));
    CHECK_EQ(eval(in, "$calc(17 % 5)"), std::string("2"));
    CHECK_EQ(eval(in, "$calc(10/4)"), std::string("2.5"));
    CHECK_EQ(eval(in, "$abs(-7)"), std::string("7"));
    CHECK_EQ(eval(in, "$int(7.9)"), std::string("7"));
    CHECK_EQ(eval(in, "$round(3.14159,2)"), std::string("3.14"));

    // Tokens.
    CHECK_EQ(eval(in, "$gettok(a.b.c,2,46)"), std::string("b"));
    CHECK_EQ(eval(in, "$gettok(a.b.c,-1,46)"), std::string("c"));
    CHECK_EQ(eval(in, "$numtok(a b c d,32)"), std::string("4"));
    CHECK_EQ(eval(in, "$addtok(a.b,c,46)"), std::string("a.b.c"));
    CHECK_EQ(eval(in, "$addtok(a.b,a,46)"), std::string("a.b"));  // already present
    CHECK_EQ(eval(in, "$istok(a.b.c,b,46)"), std::string("$true"));
    CHECK_EQ(eval(in, "$istok(a.b.c,z,46)"), std::string("$false"));

    // $+ concatenation drops the surrounding spaces.
    CHECK_EQ(eval(in, "hello $+ world"), std::string("helloworld"));
    CHECK_EQ(eval(in, "$str(a,2) $+ b"), std::string("aab"));
    CHECK_EQ(eval(in, "x$+y"), std::string("xy"));
    CHECK_EQ(eval(in, "$+(abc,def,ghi)"), std::string("abcdefghi"));
    CHECK_EQ(eval(in, "$+($str(a,2),b)"), std::string("aab"));

    // Nested identifiers and brackets.
    CHECK_EQ(eval(in, "$len($str(a,4))"), std::string("4"));
    CHECK_EQ(eval(in, "$calc($len(abc) + 2)"), std::string("5"));
    // [ ] evaluates only when it contains $/% (so regex [a-z] stays literal).
    CHECK_EQ(eval(in, "x[$calc(2+3)]y"), std::string("x5y"));
    CHECK_EQ(eval(in, "[a-z]+"), std::string("[a-z]+"));

    // $me from the host.
    CHECK_EQ(eval(in, "$me"), std::string("jawsh"));

    // $iif condition.
    CHECK_EQ(eval(in, "$iif(3 > 2,yes,no)"), std::string("yes"));
    CHECK_EQ(eval(in, "$iif(1 > 2,yes,no)"), std::string("no"));

    // Variables and parameters.
    {
        Scope s;
        s.params = {"alpha", "beta", "gamma"};
        CHECK_EQ(in.evaluate("$1", s), std::string("alpha"));
        CHECK_EQ(in.evaluate("$0", s), std::string("3"));
        CHECK_EQ(in.evaluate("$2-", s), std::string("beta gamma"));
        CHECK_EQ(in.evaluate("$1-2", s), std::string("alpha beta"));
    }

    // /set and %var substitution.
    {
        Scope s;
        in.execute("set %greeting hello there", s);
        CHECK_EQ(in.evaluate("%greeting", s), std::string("hello there"));
        in.execute("inc %n 5", s);
        in.execute("inc %n", s);
        CHECK_EQ(in.evaluate("%n", s), std::string("6"));
        in.execute("dec %n 2", s);
        CHECK_EQ(in.evaluate("%n", s), std::string("4"));
        in.execute("unset %n", s);
        CHECK_EQ(in.evaluate("%n", s), std::string(""));
    }

    // /echo and /say reach the host.
    {
        host.echoed.clear();
        Scope s;
        in.execute("echo -a hello $calc(6*7)", s);
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("hello 42"));
        host.sent.clear();
        in.execute("msg #chan hi there", s);
        CHECK_EQ(host.sent[0], std::string("#chan\thi there"));
        host.actions.clear();
        in.execute("me waves", s);
        CHECK_EQ(host.actions[0], std::string("#test\twaves"));
    }

    // if / elseif / else.
    {
        host.echoed.clear();
        Scope s;
        in.execute("set %x 5", s);
        in.execute("if (%x > 10) { echo big } elseif (%x > 3) { echo medium } else { echo small }", s);
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("medium"));
    }

    // Braceless if (mIRC allows `if (x) command`).
    {
        host.echoed.clear();
        Scope s;
        in.execute("set %x 7", s);
        in.execute("if (%x > 5) echo braceless-yes", s);
        in.execute("if (%x < 5) echo should-not-run", s);
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("braceless-yes"));
    }

    // while loop with a counter.
    {
        host.echoed.clear();
        Scope s;
        in.execute("set %i 0 | while (%i < 3) { echo line %i | inc %i }", s);
        CHECK_EQ(host.echoed.size(), 3u);
        CHECK_EQ(host.echoed[0], std::string("line 0"));
        CHECK_EQ(host.echoed[2], std::string("line 2"));
    }

    // Aliases with parameters and /return.
    {
        in.defineAlias("double", "return $calc($1 * 2)");
        CHECK_EQ(in.callAlias("double", {"21"}), std::string("42"));

        host.echoed.clear();
        in.defineAlias("greet", "echo hi $1 from $me");
        Scope s;
        in.execute("greet bob", s);
        CHECK_EQ(host.echoed[0], std::string("hi bob from jawsh"));
    }

    // An alias used as an identifier is left literal when unknown.
    CHECK_EQ(eval(in, "$nosuchident(1)"), std::string("$nosuchident(1)"));

    // Conditions: operators and boolean combinations.
    {
        Scope s;
        s.locals["a"] = "5";
        CHECK(in.evalCondition("%a == 5", s));
        CHECK(in.evalCondition("05 == 5", s));    // == compares numerically when both are numbers
        CHECK(in.evalCondition("abc == ABC", s));  // ...and case-insensitively otherwise
        CHECK(in.evalCondition("%a >= 5 && %a < 10", s));
        CHECK(!in.evalCondition("%a > 5", s));
        CHECK(in.evalCondition("abc isin xxabcxx", s));
        CHECK(in.evalCondition("he?lo iswm hello", s));
        CHECK(in.evalCondition("5 isnum", s));
        CHECK(!in.evalCondition("!(%a == 5)", s));
    }

    // Remote scripts: load aliases and on-events, then fire an event.
    {
        host.echoed.clear();
        host.sent.clear();
        in.loadScript(
            "alias ping { echo pong to $1 }\n"
            "on *:TEXT:!hello*:#: { msg $chan hi $nick }\n"
            "on *:TEXT:*badword*:#: { echo [filter] $nick said a bad word | halt }\n");
        CHECK(in.hasEvent("text"));

        // A matching channel message triggers the reply.
        Scope ev;
        ev.nick = "bob";
        ev.chan = "#test";
        ev.target = "#test";
        ev.params = {"!hello", "there"};
        in.raiseEvent("TEXT", "!hello there", ev);
        CHECK_EQ(host.sent.size(), 1u);
        CHECK_EQ(host.sent[0], std::string("#test\thi bob"));

        // A non-matching message does nothing.
        host.sent.clear();
        Scope ev2;
        ev2.nick = "carol";
        ev2.chan = "#test";
        ev2.target = "#test";
        in.raiseEvent("TEXT", "just chatting", ev2);
        CHECK_EQ(host.sent.size(), 0u);

        // The loaded alias works.
        CHECK(in.hasAlias("ping"));
        host.echoed.clear();
        CHECK_EQ(in.callAlias("ping", {"dave"}), std::string());
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("pong to dave"));
    }

    // Custom identifiers: an alias called as $name, with $prop.
    {
        in.defineAlias("square", "return $calc($1 * $1)");
        CHECK_EQ(eval(in, "$square(5)"), std::string("25"));
        in.defineAlias("info", "return $iif($prop == size,big,small)");
        CHECK_EQ(eval(in, "$info(x).size"), std::string("big"));
        CHECK_EQ(eval(in, "$info(x).color"), std::string("small"));
    }

    // Token extras.
    CHECK_EQ(eval(in, "$findtok(a.b.c.b,b,1,46)"), std::string("2"));
    CHECK_EQ(eval(in, "$remtok(a.b.c,b,1,46)"), std::string("a.c"));
    CHECK_EQ(eval(in, "$puttok(a.b.c,X,2,46)"), std::string("a.X.c"));
    CHECK_EQ(eval(in, "$instok(a.c,b,2,46)"), std::string("a.b.c"));
    CHECK_EQ(eval(in, "$qt(hello world)"), std::string("\"hello world\""));
    CHECK_EQ(eval(in, "$qt(oneword)"), std::string("oneword"));

    // Hash tables.
    {
        Scope s;
        in.execute("hmake mytable | hadd mytable alice 30 | hadd mytable bob 25", s);
        CHECK_EQ(eval(in, "$hget(mytable,alice)"), std::string("30"));
        CHECK_EQ(eval(in, "$hget(mytable,bob)"), std::string("25"));
        CHECK_EQ(eval(in, "$hget(mytable,1).item"), std::string("alice"));
        CHECK_EQ(eval(in, "$hget(mytable,2).data"), std::string("25"));
        in.execute("hinc mytable alice 5", s);
        CHECK_EQ(eval(in, "$hget(mytable,alice)"), std::string("35"));
        in.execute("hdel mytable alice", s);
        CHECK_EQ(eval(in, "$hget(mytable,alice)"), std::string(""));
        CHECK_EQ(eval(in, "$hget(mytable,1).item"), std::string("bob"));
        in.execute("hfree mytable", s);
        CHECK_EQ(eval(in, "$hget(mytable)"), std::string("0"));
    }

    // Regex: match count and captures.
    {
        CHECK_EQ(eval(in, "$regex(hello123world,(\\d+))"), std::string("1"));
        CHECK_EQ(eval(in, "$regml(1)"), std::string("123"));
        // Named, global: capture all matches' groups.
        CHECK_EQ(eval(in, "$regex(nums,a1 b2 c3,/([a-z])(\\d)/g)"), std::string("3"));
        CHECK_EQ(eval(in, "$regml(nums,1)"), std::string("a"));
        CHECK_EQ(eval(in, "$regml(nums,2)"), std::string("1"));
        CHECK_EQ(eval(in, "$regml(nums,3)"), std::string("b"));
        // Case-insensitive flag.
        CHECK_EQ(eval(in, "$regex(FOO,/foo/i)"), std::string("1"));
        // No match.
        CHECK_EQ(eval(in, "$regex(abc,\\d+)"), std::string("0"));
        // $regsub replacement.
        CHECK_EQ(eval(in, "$regsub(a1b2c3,/\\d/g,#)"), std::string("a#b#c#"));
    }

    // Math/token/path extras drawn from real scripts (jirc/NeonScript).
    CHECK_EQ(eval(in, "$ceil(3.2)"), std::string("4"));
    CHECK_EQ(eval(in, "$floor(3.8)"), std::string("3"));
    CHECK_EQ(eval(in, "$sqrt(144)"), std::string("12"));
    CHECK_EQ(eval(in, "$base(255,10,16)"), std::string("FF"));  // mIRC emits uppercase hex
    CHECK_EQ(eval(in, "$base(ff,16,10)"), std::string("255"));
    CHECK_EQ(eval(in, "$duration(3661)"), std::string("1hr 1min 1sec"));
    CHECK_EQ(eval(in, "$duration(90,2)"), std::string("90"));
    CHECK_EQ(eval(in, "$deltok(a.b.c.d,2,46)"), std::string("a.c.d"));
    CHECK_EQ(eval(in, "$sorttok(c.a.b,46)"), std::string("a.b.c"));
    CHECK_EQ(eval(in, "$sorttok(3.1.2,46,n)"), std::string("1.2.3"));
    CHECK_EQ(eval(in, "$nopath(/home/jawsh/file.mrc)"), std::string("file.mrc"));
    CHECK_EQ(eval(in, "$nofile(/home/jawsh/file.mrc)"), std::string("/home/jawsh/"));
    CHECK_EQ(eval(in, "$mask(nick!user@host.com,2)"), std::string("*!*@host.com"));
    CHECK_EQ(eval(in, "$strip(" "\x02" "bold" "\x03" "4red" "\x0F" ")"), std::string("boldred"));

    // $isalias.
    CHECK_EQ(eval(in, "$isalias(square)"), std::string("$true"));
    CHECK_EQ(eval(in, "$isalias(nosuch)"), std::string("$false"));

    // Files and INI.
    {
        Scope s;
        in.execute("write log.txt first line", s);
        in.execute("write log.txt second line", s);
        CHECK_EQ(eval(in, "$lines(log.txt)"), std::string("2"));
        CHECK_EQ(eval(in, "$read(log.txt,2)"), std::string("second line"));
        CHECK_EQ(eval(in, "$isfile(log.txt)"), std::string("$true"));
        CHECK_EQ(eval(in, "$isfile(nope.txt)"), std::string("$false"));
        CHECK(std::stoi(eval(in, "$file(log.txt).size")) > 0);
        in.execute("write -c log.txt only line", s);
        CHECK_EQ(eval(in, "$lines(log.txt)"), std::string("1"));

        in.execute("writeini cfg.ini Settings nick jawsh", s);
        in.execute("writeini cfg.ini Settings theme neon", s);
        in.execute("writeini cfg.ini Window width 800", s);
        CHECK_EQ(eval(in, "$readini(cfg.ini,Settings,nick)"), std::string("jawsh"));
        CHECK_EQ(eval(in, "$readini(cfg.ini,Settings,theme)"), std::string("neon"));
        CHECK_EQ(eval(in, "$readini(cfg.ini,Window,width)"), std::string("800"));
        CHECK_EQ(eval(in, "$readini(cfg.ini,Settings,nick)"), std::string("jawsh"));
        CHECK_EQ(eval(in, "$ini(cfg.ini,1)"), std::string("Settings"));
        CHECK_EQ(eval(in, "$ini(cfg.ini,Settings,2)"), std::string("theme"));
        // Updating an existing item keeps the file coherent.
        in.execute("writeini cfg.ini Settings theme dark", s);
        CHECK_EQ(eval(in, "$readini(cfg.ini,Settings,theme)"), std::string("dark"));
        CHECK_EQ(eval(in, "$readini(cfg.ini,Settings,nick)"), std::string("jawsh"));
    }

    // /timer passes repeat, interval and the (raw, unevaluated) command to the host.
    {
        host.timers.clear();
        Scope s;
        in.execute("timer 3 0.5 echo tick %count", s);
        CHECK_EQ(host.timers.size(), 1u);
        CHECK(host.timers[0].rfind("3\t", 0) == 0);
        CHECK(host.timers[0].find("echo tick %count") != std::string::npos);
    }

    // Time identifiers are non-empty and formatted.
    CHECK(!eval(in, "$time").empty());
    CHECK(!eval(in, "$date").empty());
    CHECK(eval(in, "$asctime(0)").find(":") != std::string::npos);  // formatted (year is tz-dependent)

    // Binary variables.
    {
        Scope s;
        in.execute("bset -t &b 1 Hello", s);
        CHECK_EQ(eval(in, "$bvar(&b,1)"), std::string("72"));   // 'H'
        CHECK_EQ(eval(in, "$bvar(&b).len"), std::string("5"));
        CHECK_EQ(eval(in, "$bvar(&b,1-5)"), std::string("72 101 108 108 111"));
        in.execute("bset &b 1 74", s);  // overwrite 'H' with 'J'
        CHECK_EQ(eval(in, "$bvar(&b,1)"), std::string("74"));
        in.execute("bunset &b", s);
        CHECK_EQ(eval(in, "$bvar(&b).len"), std::string("0"));
    }

    // /signal fires on SIGNAL:<name> with params and $signal.
    {
        host.echoed.clear();
        in.loadScript("on *:SIGNAL:ping:{ echo got signal $signal with $1 }\n");
        Scope s;
        in.execute("signal ping hello", s);
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("got signal ping with hello"));
    }

    // halt stops the rest of the script.
    {
        host.echoed.clear();
        Scope s;
        in.execute("echo one | halt | echo two", s);
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("one"));
    }

    // --- regression: control flow, token ranges and evaluation operator ---
    auto run = [&](const std::string& script) -> std::string {
        host.echoed.clear();
        Scope s;
        in.execute(script, s);
        return host.echoed.empty() ? std::string() : host.echoed.back();
    };

    // if / elseif / else across the three layouts mIRC accepts.
    CHECK_EQ(run("if (1 == 2) { echo A } | elseif (1 == 1) { echo B } | else { echo C }"), std::string("B"));
    CHECK_EQ(run("if (1 == 2) {\n echo A \n}\nelseif (1 == 1) {\n echo B \n}\nelse {\n echo C \n}"),
             std::string("B"));
    CHECK_EQ(run("if (0) {\n echo A \n}\nelse {\n echo C \n}"), std::string("C"));
    CHECK_EQ(run("if (5 > 3) echo big | else echo small"), std::string("big"));

    // == / != are case-insensitive; === is case-sensitive (mIRC semantics).
    CHECK_EQ(run("if (ABC == abc) echo eq | else echo ne"), std::string("eq"));
    CHECK_EQ(run("if (ABC != abc) echo ne | else echo eq"), std::string("eq"));
    CHECK_EQ(run("if (ABC === abc) echo eq | else echo ne"), std::string("ne"));

    // break and continue in while loops.
    CHECK_EQ(run("var %i = 0 | while (%i < 10) { inc %i | if (%i == 3) break } | echo %i"), std::string("3"));
    CHECK_EQ(run("var %i = 0 | var %n = 0 | while (%i < 5) { inc %i | if (%i == 3) continue | inc %n } | echo %n"),
             std::string("4"));

    // /tokenize populates $1-$N for the current scope.
    CHECK_EQ(run("tokenize 32 alpha beta gamma | echo $2"), std::string("beta"));

    // $gettok ranges: N-M and N- (to the end).
    CHECK_EQ(eval(in, "$gettok(a.b.c.d,2-3,46)"), std::string("b.c"));
    CHECK_EQ(eval(in, "$gettok(a.b.c.d,2-,46)"), std::string("b.c.d"));
    CHECK_EQ(eval(in, "$gettok(a.b.c.d,-2,46)"), std::string("c"));

    // $wildtok and $reptok.
    CHECK_EQ(eval(in, "$wildtok(aa ab ba,a*,2,32)"), std::string("ab"));
    CHECK_EQ(eval(in, "$wildtok(aa ab ba,a*,0,32)"), std::string("2"));
    CHECK_EQ(eval(in, "$reptok(a b c,b,X,32)"), std::string("a X c"));

    // $(text,N): the explicit N-pass evaluation operator.
    {
        Scope s;
        s.locals["a"] = "%b";
        s.locals["b"] = "%c";
        s.locals["c"] = "deep";
        CHECK_EQ(in.evaluate("$(%a,3)", s), std::string("deep"));
    }

    // IRC-state identifiers: $nick(#,N), $chan(N), $comchan, $ison.
    {
        host.chanNicks = {"alice", "bob", "carol"};
        host.myChans = {"#a", "#b"};
        CHECK_EQ(eval(in, "$nick(#test,0)"), std::string("3"));   // count
        CHECK_EQ(eval(in, "$nick(#test,2)"), std::string("bob"));  // Nth
        CHECK_EQ(eval(in, "$chan(0)"), std::string("2"));
        CHECK_EQ(eval(in, "$chan(2)"), std::string("#b"));
        CHECK_EQ(eval(in, "$ison(#test,bob)"), std::string("$true"));
        CHECK_EQ(eval(in, "$ison(#test,dave)"), std::string("$false"));
        CHECK_EQ(eval(in, "$comchan(bob,0)"), std::string("2"));
        host.chanNicks.clear();
        host.myChans.clear();
    }

    // Standard IRC commands map to the right raw lines / sends.
    {
        Scope s;
        host.raws.clear();
        in.execute("join #chan", s);
        CHECK_EQ(host.raws.back(), std::string("JOIN #chan"));
        in.execute("nick newnick", s);
        CHECK_EQ(host.raws.back(), std::string("NICK newnick"));
        in.execute("topic #chan hello world", s);
        CHECK_EQ(host.raws.back(), std::string("TOPIC #chan :hello world"));
        in.execute("kick #chan baddie go away", s);
        CHECK_EQ(host.raws.back(), std::string("KICK #chan baddie :go away"));
        in.execute("notice bob hi there", s);
        CHECK_EQ(host.raws.back(), std::string("NOTICE bob :hi there"));
        in.execute("away back soon", s);
        CHECK_EQ(host.raws.back(), std::string("AWAY :back soon"));
        in.execute("back", s);
        CHECK_EQ(host.raws.back(), std::string("AWAY"));
        host.myChans = {"#a", "#b"};
        host.sent.clear();
        in.execute("amsg hello all", s);
        CHECK_EQ(host.sent.size(), 2u);
        host.myChans.clear();
    }

    // New identifiers: $min/$max, base64 $encode/$decode, $longip, $hfind, $v1/$v2.
    {
        CHECK_EQ(eval(in, "$min(3,7,1,9)"), std::string("1"));
        CHECK_EQ(eval(in, "$max(3,7,1,9)"), std::string("9"));
        CHECK_EQ(eval(in, "$encode(hello)"), std::string("aGVsbG8="));
        CHECK_EQ(eval(in, "$decode(aGVsbG8=)"), std::string("hello"));
        CHECK_EQ(eval(in, "$longip(1.2.3.4)"), std::string("16909060"));
        CHECK_EQ(eval(in, "$longip(16909060)"), std::string("1.2.3.4"));
        Scope s;
        in.execute("hmake t 10 | hadd t apple red | hadd t apricot orange | hadd t banana yellow", s);
        CHECK_EQ(eval(in, "$hfind(t,ap*,0,w)"), std::string("2"));
        CHECK_EQ(eval(in, "$hfind(t,ap*,1,w)"), std::string("apple"));
        CHECK_EQ(eval(in, "$hfind(t,ap*,2,w)"), std::string("apricot"));
    }
    {
        host.echoed.clear();
        Scope s;
        in.execute("if (5 > 3) echo $v1 $v2", s);
        CHECK_EQ(host.echoed.back(), std::string("5 3"));
    }

    // Scriptable popup menus: `menu nicklist { ... }` parses into items/submenus.
    {
        // mIRC menus nest by leading dots, not braces.
        in.loadScript(
            "menu nicklist {\n"
            "  Whois:whois $1\n"
            "  -\n"
            "  Kick:kick $chan $1\n"
            "  Ops\n"
            "  .Op:mode $chan +o $1\n"
            "  .Deop:mode $chan -o $1\n"
            "  ..Force Deop:mode $chan -o $1\n"
            "}\n");
        const auto* m = in.popupMenu("nicklist");
        CHECK(m != nullptr);
        if (m) {
            CHECK_EQ(m->size(), 4u);  // Whois, separator, Kick, Ops
            CHECK_EQ((*m)[0].caption, std::string("Whois"));
            CHECK_EQ((*m)[0].command, std::string("whois $1"));
            CHECK((*m)[1].separator);
            CHECK_EQ((*m)[3].caption, std::string("Ops"));
            if ((*m)[3].children.size() == 2u) {
                CHECK_EQ((*m)[3].children[1].caption, std::string("Deop"));
                // third dot level nests under Deop
                CHECK_EQ((*m)[3].children[1].children.size(), 1u);
            } else {
                CHECK_EQ((*m)[3].children.size(), 2u);
            }
        }
    }

    // $? / $$? input prompts (used in menus like ChanServ Register).
    {
        host.inputAnswer = "#mychan";
        CHECK_EQ(eval(in, "$?=\"Channel\""), std::string("#mychan"));
        CHECK_EQ(host.lastPrompt, std::string("Channel"));
        CHECK_EQ(eval(in, "register $$?=\"#Channel\" $$?=\"Password\""),
                 std::string("register #mychan #mychan"));
        CHECK_EQ(eval(in, "$input(Enter name,p)"), std::string("#mychan"));
        host.inputAnswer = "answer";
    }

    // Colour/rgb/regsubex (from the corpus crawl gap analysis).
    CHECK_EQ(eval(in, "$rgb(255,128,0)"), std::string("16744448"));
    CHECK_EQ(eval(in, "$rgb(16744448)"), std::string("255,128,0"));
    CHECK_EQ(eval(in, "$color(red)"), std::string("4"));
    CHECK_EQ(eval(in, "$colour(blue)"), std::string("2"));
    CHECK_EQ(eval(in, "$color(7)"), std::string("7"));
    CHECK_EQ(eval(in, "$regsubex(a1b2,/(\\d)/g,<\\1>)"), std::string("a<1>b<2>"));
    CHECK_EQ(eval(in, "$regsubex(abc,/b/,X)"), std::string("aXc"));

    // Context/connection identifiers (from the corpus crawl gap analysis).
    CHECK_EQ(eval(in, "$network"), std::string("TestNet"));
    CHECK_EQ(eval(in, "$server"), std::string("irc.test"));
    CHECK_EQ(eval(in, "$cid"), std::string("1"));
    CHECK_EQ(eval(in, "$version"), std::string("7.85"));
    CHECK_EQ(eval(in, "$bytes(1536)"), std::string("1.5 KB"));
    CHECK_EQ(eval(in, "$bytes(1536).suf"), std::string("KB"));
    CHECK(!eval(in, "$timestamp").empty());
    {  // $eval(text,N): N-pass evaluation (named form of $())
        Scope s;
        s.locals["a"] = "%b";
        s.locals["b"] = "deep";
        CHECK_EQ(in.evaluate("$eval(%a,2)", s), std::string("deep"));
    }

    // Math + bitwise identifiers.
    CHECK_EQ(eval(in, "$sin(0)"), std::string("0"));
    CHECK_EQ(eval(in, "$cos(0)"), std::string("1"));
    CHECK_EQ(eval(in, "$exp(0)"), std::string("1"));
    CHECK(eval(in, "$pi").rfind("3.14159", 0) == 0);
    CHECK_EQ(eval(in, "$and(12,10)"), std::string("8"));
    CHECK_EQ(eval(in, "$or(12,10)"), std::string("14"));
    CHECK_EQ(eval(in, "$xor(12,10)"), std::string("6"));
    CHECK_EQ(eval(in, "$isbit(5,1)"), std::string("$true"));   // bit 1 (LSB) of 0b101
    CHECK_EQ(eval(in, "$isbit(5,2)"), std::string("$false"));  // bit 2 clear
    CHECK_EQ(eval(in, "$biton(0,3)"), std::string("4"));       // set bit 3 → 0b100
    CHECK_EQ(eval(in, "$bitoff(7,1)"), std::string("6"));      // clear bit 1 of 0b111

    // /hop and /ban translate to the right raw IRC.
    {
        host.raws.clear();
        Scope s;
        s.target = "#chan";  // not used; active target drives it
        in.execute("ban baduser", s);  // active target is #test (host.active)
        CHECK_EQ(host.raws.back(), std::string("MODE #test +b baduser!*@*"));
        in.execute("unban baduser!*@*", s);
        CHECK_EQ(host.raws.back(), std::string("MODE #test -b baduser!*@*"));
        in.execute("hop", s);
        CHECK_EQ(host.raws.back(), std::string("JOIN #test"));
    }

    // Directory identifiers.
    CHECK_EQ(eval(in, "$mircdir"), std::string("/app/"));
    CHECK_EQ(eval(in, "$mircdir(logs/x.txt)"), std::string("/app/logs/x.txt"));
    CHECK_EQ(eval(in, "$scriptdir"), std::string("/scripts/"));
    CHECK_EQ(eval(in, "$gmt"), std::string("1000"));

    // on NICK fires with $nick (old) and $newnick (new).
    {
        host.echoed.clear();
        in.loadScript("on *:NICK:{ echo $nick is now $newnick }\n");
        Scope s;
        s.nick = "oldguy";
        s.newnick = "newguy";
        in.raiseEvent("NICK", "newguy", s);
        CHECK_EQ(host.echoed.back(), std::string("oldguy is now newguy"));
    }

    // Unbounded recursion is caught, not a crash (reports an error and stops).
    {
        host.errors.clear();
        in.defineAlias("recx", "recx");
        Scope s;
        in.execute("recx", s);
        CHECK(!host.errors.empty());
    }

    // Dialog table parsing: caption-less controls (list/combo) put the id first,
    // captioned controls (button/edit) put a "caption" first. (Regression: a
    // `list 1, x y w h` was mis-parsed as id=x with no geometry.)
    {
        const std::string body =
            "  title \"T\"\n"
            "  size -1 -1 125 152\n"
            "  list 1, 4 8 116 36, size\n"
            "  button \"Add\", 3, 5 44 19 10\n"
            "  edit \"\", 8, 4 66 115 10, autohs\n"
            "  combo 10, 2 2 50 80, drop\n";
        DialogTable t = parseDialogTable("blist", body);
        const DialogControl* list = t.findId(1);
        CHECK(list != nullptr);
        CHECK_EQ(list->type, std::string("list"));
        CHECK_EQ(list->x, 4);
        CHECK_EQ(list->w, 116);
        CHECK_EQ(list->h, 36);
        const DialogControl* btn = t.findId(3);
        CHECK(btn != nullptr);
        CHECK_EQ(btn->caption, std::string("Add"));
        CHECK_EQ(btn->x, 5);
        const DialogControl* edit = t.findId(8);
        CHECK(edit != nullptr);
        CHECK_EQ(edit->caption, std::string(""));
        const DialogControl* combo = t.findId(10);
        CHECK(combo != nullptr);
        CHECK_EQ(combo->w, 50);
    }

    // $read line access + $ifmatch, and the s/w scan switches.
    {
        host.files["/scripts/bl.txt"] = "spambot99 Flooding\ntroll!*@*.x Harass\nbaduser Ads\n";
        Scope s;
        CHECK_EQ(in.evaluate("$read(/scripts/bl.txt,2)", s), std::string("troll!*@*.x Harass"));
        CHECK_EQ(in.evaluate("$ifmatch", s), std::string("troll!*@*.x Harass"));
        CHECK_EQ(in.evaluate("$readn", s), std::string("2"));
        // s: line beginning with the match text.
        CHECK_EQ(in.evaluate("$read(/scripts/bl.txt,s,baduser)", s), std::string("baduser Ads"));
        CHECK_EQ(in.evaluate("$gettok($ifmatch,1,32)", s), std::string("baduser"));
        // no match → empty.
        CHECK_EQ(in.evaluate("$read(/scripts/bl.txt,s,nobody)", s), std::string(""));
    }

    // Dotted variable names (%blist.msg is one variable, not %blist + ".msg").
    {
        Scope s;
        in.execute("set %blist.msg Hello there", s);
        CHECK_EQ(in.evaluate("%blist.msg", s), std::string("Hello there"));
        CHECK_EQ(in.evaluate("value is %blist.msg.", s), std::string("value is Hello there."));  // trailing dot kept as text
    }

    // $scriptdir<path> / $mircdir<path> shorthand (the trailing .ext is folded back).
    CHECK_EQ(eval(in, "$scriptdirmdx.dll"), std::string("/scripts/mdx.dll"));
    CHECK_EQ(eval(in, "$scriptdirviews.mdx"), std::string("/scripts/views.mdx"));
    CHECK_EQ(eval(in, "$mircdirlogs.txt"), std::string("/app/logs.txt"));

    // Hash tables: /hadd then /hsave and /hload round-trips through the (fake) FS.
    {
        Scope s;
        in.execute("hmake bl 10", s);
        in.execute("hadd bl punish kb", s);
        in.execute("hadd bl temptime 360", s);
        in.execute("hsave -o bl /scripts/bl.hsh", s);
        in.execute("hfree bl", s);
        CHECK_EQ(in.evaluate("$hget(bl,punish)", s), std::string(""));  // freed
        in.execute("hload bl /scripts/bl.hsh", s);
        CHECK_EQ(in.evaluate("$hget(bl,punish)", s), std::string("kb"));
        CHECK_EQ(in.evaluate("$hget(bl,temptime)", s), std::string("360"));
    }

    // $shortfn passes the path through; $lock is $false; $isid distinguishes
    // $alias(...) from a command call.
    CHECK_EQ(eval(in, "$shortfn(/a/b/c.dll)"), std::string("/a/b/c.dll"));
    CHECK_EQ(eval(in, "$lock(dll)"), std::string("$false"));
    {
        host.echoed.clear();
        in.defineAlias("amid", "if ($isid) return ID | echo CMD");
        Scope s;
        CHECK_EQ(in.evaluate("$amid", s), std::string("ID"));
        in.execute("amid", s);
        CHECK_EQ(host.echoed.back(), std::string("CMD"));
    }

    return check::summary();
}
