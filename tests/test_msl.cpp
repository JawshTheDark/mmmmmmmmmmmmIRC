// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <vector>

#include "check.h"
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
    std::string startTimer(const std::string&, int, double, const std::string&) override { return "timer1"; }
    void stopTimer(const std::string&) override {}
    void scriptError(const std::string& t) override { errors.push_back(t); }
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
        CHECK(in.evalCondition("%a >= 5 && %a < 10", s));
        CHECK(!in.evalCondition("%a > 5", s));
        CHECK(in.evalCondition("abc isin xxabcxx", s));
        CHECK(in.evalCondition("he?lo iswm hello", s));
        CHECK(in.evalCondition("5 isnum", s));
        CHECK(!in.evalCondition("!(%a == 5)", s));
    }

    // halt stops the rest of the script.
    {
        host.echoed.clear();
        Scope s;
        in.execute("echo one | halt | echo two", s);
        CHECK_EQ(host.echoed.size(), 1u);
        CHECK_EQ(host.echoed[0], std::string("one"));
    }

    return check::summary();
}
