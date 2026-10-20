# mmmmmmmmmmmmIRC — handoff / continuation

For a person or agent picking this up. A cross-platform (Linux + Windows) mIRC-style IRC client for
Lurker, C++20 + Qt 6, MPL-2.0, clean-room. Entry points: [CLAUDE.md](../CLAUDE.md) (rules),
[docs/PLAN.md](PLAN.md) (phases), [docs/PARITY.md](PARITY.md) (scoreboard, ~197/1129 done).

## Run it

```bash
cmake --preset dev            # or: cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.3/gcc_64
cmake --build --preset dev
./build/src/gui/mmmmmmmmmmmmirc
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
unbounded alias reports an error instead of crashing). Phase 3 (mostly): theming — 7 shipped skins
(Default/Dark/OLED/Solarized/Matrix/Amber/BeOS) with a themed MDI backdrop, configurable chat
font/size (live), the 16-colour palette wired into message rendering, per-nick colouring, window
background colour/image, theme **import/export**; full tree Options dialog, top toolbar, app icon,
right-click **context menus** on the nicklist (query/whois/op/kick/ban/CTCP) and the networks tree
(part/close/disconnect), and channel/query **logging** to disk. Phase 5.5: **CI** (Linux + Windows
build/test on every push/PR) and **Releases** (tag `v*` → AppImage + Windows zip) via GitHub Actions.

Validated: [NeonScript](https://github.com/jawshthedark/neonscript) loads fully (1024 aliases, 457
handlers); jirc's Anope/abaexp/ppfserv scripts parse correctly.

**DCX addon (#19), through phase 2** (as of v0.1.75): the `dcx.dll` emulation renders DCX XML layouts
(`DcxView`: nested pane/panel box-model + the common controls), and scripts can drive them —
`/xdid -v` (set), `-a` (add item / append to a multiline edit), `-r` (clear), `-c` (check/select),
`-h`/`-s` (hide/show), `-Z` (scroll to bottom), across a list of ids; `$xdid(dialog,id).id/.pid/.text`
read control state; and controls are **interactive** — a button click or list/tree/tab selection fires
the dialog's `dcx Mark`'d alias as `<alias> <dialog> <event> <id> [arg]` (sclick / selchange). This was
unblocked by implementing **`/returnex`** (the `alias x { if ($isid) returnex … }` identifier idiom —
which fixes *every* custom `$identifier` alias, not just DCX) and trimming the identifier-form proc.
Also shipped alongside: the editbox now follows the active theme's colours.

## Next (priority order)

1. **DCC through Lurker** (owner requirement): `soju.im/FILEHOST` + `/api/dcc` — see DESIGN.md. The
   toolbar DCC buttons + Options ▸ DCC page are in; the server-side bridge is the remaining work.
2. IRC-state identifiers still needing Host wiring: `$ial`/`$ialchan` (**blocked on Lurker** — its WS
   events carry no user@host; works only on direct-IRC/bouncer, so low priority), `$nickcount`,
   `/onotice`. *(Done: `$nick(#,N)`, `$comchan`, `$level` (#30), and `on`-event `/halt` of default
   processing (v0.1.76) — a handler's /halt now suppresses the client's default display.)*
3. More of the identifier tail (see PARITY.md `planned`, ordered by corpus use). *(Done: `$regsubex`,
   `$regsub`, `$hfind`, `$regml`.)*
4. Wire the remaining Options consumers: highlight words → highlight styling, flood protection on
   send, join/part/quit display toggle, reconnect options. (Timestamp format, scrollback, strip/colour
   nicks, background, logging are already live.)
5. **Per-channel** theming (background/font overrides keyed by net+channel) — Phase 3.6; global is done.
6. Run the oracle (tools/oracle) to pin evaluation edge cases.
7. **DCX long-tail** (#19 phase 2 remainder, *lower priority — paused deliberately*): `xdialog`
   dialog-level ops and `$xdialog(dialog, name).id` control-by-name (needs parsing control `name=`
   attributes, which `DcxView::Node` doesn't keep yet); `xdid -t` (listview columns) and `-m`
   (colorcombo fill) — **caution:** DCX overloads these switches per control type and the exact arg
   formats are ambiguous, so verify each against `scratchpad/dcx_zip/` (demo + `dcx_tools.mrc`) and skip
   anything that can't be pinned down; and upgrade the placeholder controls (toolbar, colorcombo,
   richedit, updown, embedded `@window`) from labelled boxes to real widgets. The event plumbing
   (`DcxView::controlEvent` → marked alias) is in place, so new interactive controls just need to call
   `connectControlEvents`.

## Gotchas

- Shell tests: single-quote `--msl=...` or bash eats `$identifiers`.
- `[ ]` evaluates only when it contains `$`/`%` (so regex `[a-z]` stays literal) — intentional.
- Command args evaluate at execution; `/set`'s variable *name* stays literal (mIRC semantics).
- API tokens are bouncer+`/mcp` only; the WS/REST path needs a minted session token (web password).
