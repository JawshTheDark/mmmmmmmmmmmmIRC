# Lurker Desktop — handoff / continuation

For a person or agent picking this up. A cross-platform (Linux + Windows) mIRC-style IRC client for
Lurker, C++20 + Qt 6, MPL-2.0, clean-room. Entry points: [CLAUDE.md](../CLAUDE.md) (rules),
[docs/PLAN.md](PLAN.md) (phases), [docs/PARITY.md](PARITY.md) (scoreboard, ~148/1129 done).

## Run it

```bash
cmake --preset dev            # or: cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.3/gcc_64
cmake --build --preset dev
./build/src/gui/lurker-desktop
```

Then **File ▸ Connect** (prefills from `.secrets/dev-profile.json` when present): Lurker mode uses the
WebSocket (all networks at once, password mints a token); Bouncer mode uses raw IRC on port 1025.
**File ▸ Load Script** / **Scripts Editor** load `.mrc` files; **File ▸ Options** (Alt+O) for settings;
**View ▸ Theme** for skins (incl. BeOS). Without Qt: `-DLURKER_BUILD_GUI=OFF` builds core + tests.

Dev flags: `--connect-dev`, `--script=<file|dir>`, `--msl=<line>`, `--simulate=net|nick|target|text`,
`--theme=<id>`, `--scripteditor`, `--options[=Page]`, `--export-icon=out.png[,size]`,
`--screenshot=out.png[,ms]` (with `QT_QPA_PLATFORM=offscreen`).

The top **toolbar** (clean-room icons in `ToolbarIcons.h`) and the tree **Options** dialog
(`OptionsDialog`, mIRC's Connect/IRC/DCC/Display/Colors/Sound/Mouse/Logging/Other tree) are built from
the published layout — no mIRC artwork. The app icon is drawn in `AppIcon.h`.

## Architecture

- `src/core/` — Qt-free, unit-tested. `IrcFormat`, `IrcMessage`, `IrcSession` (raw IRC state machine),
  `CommandParser`, `Completion`, and `msl/` (the scripting engine).
- `src/core/msl/` — `Interpreter` (evaluate/execute/conditions/events/hash/regex/binvars/dialogs),
  `Builtins` (identifiers + commands), `Dialog` (table parser), `Host` (the interface the engine
  reaches the world through — files, sockets, timers, dialogs, windows, IRC sends).
- `src/backend/` — `ChatBackend` (abstract) + `BouncerChatBackend` (raw IRC) and `LurkerChatBackend`
  (Lurker WS/REST via `LurkerClient`). Both feed the GUI the same signals.
- `src/gui/` — `MainWindow` (multi-network buffers, MDI, menus), `ChatView`, `ConnectDialog`,
  `ScriptDialog`/`PictureWindow`/`ScriptSockets` (script-driven), `ScriptEditor`, `OptionsDialog`,
  `Theme`, `GuiScriptHost` (implements the MSL `Host`).
- `tools/parity/` — the matrix (`data/*.tsv`, `render.py`, `import_help_index.py`, `scan_corpus.py`).
- `tools/oracle/` — differential testing against real mIRC under Wine (not yet run).

## Done

Phase 1 (client): both backends, multi-network GUI, history-on-open, unread + read-state, reconnect.
Phase 2 (MSL): evaluation, control flow (incl. braceless), aliases, custom identifiers, events/
remotes (on TEXT/ACTION/JOIN/PART/KICK/DIALOG/SIGNAL/SOCK*), ~100 identifiers, ~55 commands, hash
tables, regex, file & INI I/O, timers, sockets, signals, binary variables, dialogs, picture + custom
windows. Control flow now includes `/break` `/continue` `/tokenize`, `$(text,N)` evaluation,
`$gettok` ranges, `$wildtok`/`$reptok`, case-insensitive `==`/`!=`, and a recursion guard (an
unbounded alias reports an error instead of crashing). Phase 3 (started): theming
(Default/Dark/OLED/BeOS), configurable chat font/size (live), 16-colour palette page, Scripts Editor,
full tree Options dialog, top toolbar, app icon. Phase 5.5: **CI** (Linux + Windows build/test on every
push/PR) and **Releases** (tag `v*` → AppImage + Windows zip) via GitHub Actions.

Validated: [NeonScript](https://github.com/jawshthedark/neonscript) loads fully (1024 aliases, 457
handlers); jirc's Anope/abaexp/ppfserv scripts parse correctly.

## Next (priority order)

1. **DCC through Lurker** (owner requirement): `soju.im/FILEHOST` + `/api/dcc` — see DESIGN.md. The
   toolbar DCC buttons + Options ▸ DCC page are in; the server-side bridge is the remaining work.
2. IRC-state identifiers that need the session wired into the MSL Host: `$nick(#,N)`, `$nickcount`,
   `$ial`/`$ialchan`, `$comchan`, `$onotice`, `$level`; `on`-event `$halt` of default processing.
3. `$regsubex`, `$hfind`, more of the identifier tail (see PARITY.md `planned`, ordered by corpus use).
4. Make more Options pages take effect live (timestamps/format, join-event display, highlight words,
   logging) — the settings persist already; wire the consumers.
5. Deeper theming (per-channel backgrounds, feed the 16-colour palette into message rendering,
   import/export) — Phase 3.6.
6. Run the oracle (tools/oracle) to pin evaluation edge cases.

## Gotchas

- Shell tests: single-quote `--msl=...` or bash eats `$identifiers`.
- `[ ]` evaluates only when it contains `$`/`%` (so regex `[a-z]` stays literal) — intentional.
- Command args evaluate at execution; `/set`'s variable *name* stays literal (mIRC semantics).
- API tokens are bouncer+`/mcp` only; the WS/REST path needs a minted session token (web password).
