# The oracle: differential testing against real mIRC

When the documentation does not settle how something behaves, the answer comes from running the
case in the owner's licensed copy of mIRC (7.85) under Wine and recording the output. Only the
observed behaviour is recorded. Nothing is ever taken from the program's binary.

## One-time setup

```bash
export WINEPREFIX=$HOME/.wine-mirc
mkdir -p "$WINEPREFIX/drive_c/mirc" "$WINEPREFIX/drive_c/oracle"
cp /home/jawsh/mirc.exe "$WINEPREFIX/drive_c/mirc/"
: > "$WINEPREFIX/drive_c/mirc/mirc.ini"        # an empty mirc.ini next to mirc.exe = portable mode
cp tools/oracle/probe.mrc "$WINEPREFIX/drive_c/mirc/"
wine "$WINEPREFIX/drive_c/mirc/mirc.exe"
```

A display is required (use `xvfb-run` when there is no desktop). On start, mIRC shows its
registration/evaluation dialog; dismiss it. Then in the status window:

```
/load -rs C:\mirc\probe.mrc
/probe C:\oracle\cases.txt C:\oracle\results.txt
```

See `command_line.html` in the help file for start-up switches that may allow unattended runs.

## Running cases

- Put cases in `tests/oracle/<area>/cases.txt` (one per line; see `probe.mrc` for the format).
- Copy to `$WINEPREFIX/drive_c/oracle/cases.txt`, run `/probe`, copy `results.txt` back as
  `tests/oracle/<area>/mirc.txt`. Commit both: they are the fixture.
- The engine test runner evaluates the same cases and diffs against `mirc.txt`. Every difference
  is either a bug to fix or an intentional Linux difference documented in `notes`.

## What to probe first

Evaluation quirks (spacing around identifiers and commas, `$+`, `[ ]` order, `$eval` depth,
`$$`, `$!`), `if` operators with odd operands (empty strings, spaces, numbers with leading zeros,
`isnum` ranges), token identifiers with multi-character and out-of-range separators, `$calc`
precision and rounding, `$regsubex` markers, `$duration`/`$asctime` formats, `/timer` switch
interactions, `$read`/`/write` switch combinations.
