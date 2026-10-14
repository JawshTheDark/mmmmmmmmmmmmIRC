# lurker-desktop — working notes for agents

A clean-room, native Linux desktop IRC client for Lurker (github.com/amiantos/lurker) in the
mIRC tradition, with the goal of **100% mIRC 7.85 parity including running existing mIRC
scripts**, tailored to Lurker's bouncer and WebSocket/REST backends. C++20 + Qt 6, MPL-2.0.

Start here: [docs/PLAN.md](docs/PLAN.md) (phased plan, work packages, exit gates) →
[docs/PARITY.md](docs/PARITY.md) (live scoreboard) → [docs/DESIGN.md](docs/DESIGN.md).

## Hard rules

1. **Clean room.** Behaviour comes from public documentation (mIRC help file/online help,
   versions.txt changelog, IRC/IRCv3 specs) and black-box observation of mIRC under Wine. Never
   from disassembly or decompilation of mIRC, and never reuse mIRC assets, resource strings,
   icons, sounds or dialog layouts. The earlier REA/Ghidra exploration of `mirc.exe` in this
   project's chat history is **not** an input. Log sources per area in `docs/CLEANROOM.md`.
2. **No row without a test.** A parity row becomes `done` only with a test named in its `test`
   column. `partial` must say what is missing in `notes`.
3. **`src/core` stays Qt-free.** The scripting engine and protocol logic talk to the world through
   the `Host` interface; Qt lives in `src/gui` and `src/backend`. Tests use a fake Host.
4. **Honest about Linux.** Windows-only features are `not-possible` with a reason, or
   `substitute` with the difference documented. Nothing is silently dropped.
5. Third-party script corpora stay outside the repo (paths below); tests skip when absent.
6. Don't commit `CMakeUserPresets.json` or `build/`.

## Build and test

```bash
cmake --preset dev                 # Qt at ~/Qt/6.11.3/gcc_64 (CMakeUserPresets.json, local only)
cmake --build --preset dev
ctest --preset dev                 # all tests
QT_QPA_PLATFORM=offscreen ./build/src/gui/lurker-desktop --screenshot=out.png   # headless UI check
python3 tools/parity/render.py     # regenerate docs/PARITY.md after editing tools/parity/data/*.tsv
```

Without Qt: `cmake -S . -B build -DLURKER_BUILD_GUI=OFF` builds core + tests only.

## Layout

```
src/core/      Qt-free engine: IrcFormat, CommandParser, IrcMessage, (script/ engine to come)
src/gui/       Qt Widgets client (MainWindow, FormattedText, …)
src/backend/   Bouncer (raw IRCv3) and Lurker (WebSocket/REST) backends   [Phase 1]
tests/         unit tests (tests/check.h, no framework); script fixtures under tests/script/
tools/parity/  matrix: data/*.tsv, import_help_index.py, scan_corpus.py, render.py
tools/oracle/  probe script + notes for differential testing against real mIRC under Wine
docs/          PLAN.md, PARITY.md (generated), DESIGN.md, spec/<area>.md, CLEANROOM.md
```

## Reference material on this machine

- mIRC 7.85 (owner's licensed copy): `/home/jawsh/mirc.exe`; Wine at `/usr/bin/wine`.
- mIRC 7.72 help file: `/mnt/wd2tb/mIRC/mirc.chm` (`7z x mirc.chm -o<dir>`; index `mirc.hhk`).
  Current online help: https://www.mirc.com/help/html/ ; changelog https://www.mirc.com/versions.txt
- Lurker source and protocol docs: `~/Desktop/Projects/lurker-upstream` (`docs/CLIENT_PROTOCOL.md`,
  `docs/IRCV3.md`, `shared/`, `server/services/bouncer*`).
- Script corpora (read-only, never copied in):
  A `/mnt/wd2tb/mIRC/scripts/` · B `/mnt/md0/projects/war-gen3-orig/aowbackup3/` ·
  C `/mnt/md0/jawsh-home-backup/data/Desktop/Invincible_Script_by_Xtry-master/`
- The owner's Android Lurker clients (`~/Desktop/Projects/lurker-android-*`, MPL-2.0) are a
  behavioural reference for the WebSocket backend (`lurkerkit/` tests), not code to copy into C++.

## Conventions

- MPL-2.0 header on every source file. `-Wall -Wextra -Wpedantic`, zero warnings.
- Match the existing style: 4-space indent, 120 columns, `lurker::` / `lurker::gui::` namespaces,
  `m_` members, comments explain *why*.
- Messages shown to users are written fresh (never mIRC's wording).
- Commit messages: imperative subject, body says what changed and why; include the matrix TSV
  and regenerated PARITY.md in the same commit as the feature.
