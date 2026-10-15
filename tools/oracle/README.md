# The oracle: differential testing against real mIRC

When the documentation does not settle how something behaves, the answer comes from running the
case in the owner's licensed copy of mIRC (7.85) under Wine and recording the output. Only the
observed behaviour is recorded. Nothing is ever taken from the program's binary.

## Automated (headless) workflow

`run-headless.sh` runs a cases file through real mIRC with no desktop and no manual clicking, and
`engine-probe` runs the same cases through this repo's MSL engine. Diffing the two is the test.

```bash
# 1. capture mIRC's behaviour (real mIRC 7.85 under Wine + Xvfb)
tools/oracle/run-headless.sh tests/oracle/eval/cases.txt tests/oracle/eval/mirc.txt

# 2. evaluate the same cases with our engine
cmake --build build --target engine-probe
./build/engine-probe tests/oracle/eval/cases.txt /tmp/ours.txt

# 3. diff — every difference is a bug to fix or a documented, intentional difference
diff tests/oracle/eval/mirc.txt /tmp/ours.txt
```

`mirc.txt` is committed as the fixture, so step 1 only needs re-running when cases change. Keep the
cases file and `mirc.txt` together under `tests/oracle/<area>/`.

### Requirements

Ubuntu packages: `wine` **plus its 32-bit runtime** (`wine32:i386`, `libwine:i386` — mirc.exe is a
32-bit PE), `xvfb`, `xdotool`, `imagemagick`, `matchbox-window-manager`. `mirc.exe` defaults to
`~/mirc.exe` (override as the 3rd argument).

### How the headless runner copes with mIRC

mIRC is a 32-bit GUI app, so the runner:

- uses a **pure win32** `WINEPREFIX` (`WINEARCH=win32`) — the wow64 path on Ubuntu's wine 9.0 fails
  to load the 32-bit `ntdll`, so a 64-bit prefix can't launch mirc.exe at all;
- runs a tiny window manager (matchbox) under Xvfb for dependable focus;
- dismisses the two startup dialogs — the evaluation **About mIRC** nag and the **Connect/Options**
  dialog — by clicking their buttons at fixed coords on a 1024x768 root;
- disables mIRC's **command-safety warning** once (it pops before running a typed `/command`);
- types `/load -rs C:\mirc\probe.mrc` then `/probe C:\oracle\cases.txt C:\oracle\results.txt`, and
  waits for the `done.txt` sentinel `probe.mrc` drops beside the results.

## Cases file format

One case per line (`probe.mrc` drives this; see it for details):

- a blank line or one starting with `;` is a comment — skipped, no output;
- an expression line (anything not starting with `/`) is evaluated and written to the results as
  `<lineNo><TAB><result>`;
- a command line (starting with `/`) is executed as typed and must write its own result with
  `write %probe.out <lineNo> $+ $chr(9) $+ <text>` (the current line number is in `%probe.line`).
  `engine-probe` does not evaluate command cases — keep expression and command cases in separate files.

A case that raises a script error writes no line, so a missing line number means "mIRC reported an
error for this case".

## Manual run (reference)

If you want to drive mIRC by hand: start it (`wine .../mirc.exe` under a display), dismiss the
dialogs, then in the status window:

```
/load -rs C:\mirc\probe.mrc
/probe C:\oracle\cases.txt C:\oracle\results.txt
```

## What to probe

Evaluation quirks (spacing around identifiers and commas, `$+`, `[ ]` order, `$eval` depth, `$$`,
`$!`), `if` operators with odd operands (empty strings, spaces, numbers with leading zeros, `isnum`
ranges), token identifiers with multi-character and out-of-range separators, `$calc` precision and
rounding, `$regsubex` markers, `$duration`/`$asctime` formats, `/timer` switch interactions,
`$read`/`/write` switch combinations.
