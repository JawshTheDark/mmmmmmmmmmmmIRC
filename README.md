# Lurker Desktop

A cross-platform (Linux + Windows) desktop IRC client in the **mIRC tradition**, built for
[Lurker](https://github.com/amiantos/lurker): MDI windows, treebar, switchbar, nicklists, a real
**mIRC Scripting Language (MSL)** engine, and total theming. C++20 + Qt 6, MPL-2.0. Clean-room — no
mIRC code or assets.

The goal is an **exact mIRC replica** that also speaks Lurker's bouncer and WebSocket/REST APIs. See
[docs/PLAN.md](docs/PLAN.md) for the phased plan and [docs/PARITY.md](docs/PARITY.md) for the live
scoreboard.

## What works today

**Client (Phase 1).** Two backends behind one interface — Lurker's **WebSocket/REST** (all your
networks at once: snapshot, history, read state, uploads path) and its **IRC bouncer** (raw IRCv3,
SASL, cert pinning, reconnect/resume). Multi-network treebar, MDI channel/query windows, nicklists,
topic bars, history-on-open, unread badges with cross-device mark-read, highlight styling, clickable
links, nick/command tab-completion, input history.

**Scripting (Phase 2).** A real MSL interpreter (Qt-free core, `src/core/msl/`):

- Evaluation: `$identifiers`, `%variables`, `$N` params, `$+` / `$+()`, `[ ]` grouping, custom
  identifiers (`$myalias(...).prop`).
- Control flow: `if`/`elseif`/`else` (braced and braceless), `while`, `|` and `{ }`, aliases,
  `/return` `/halt`.
- ~95 identifiers and ~45 commands: text/token/number/time, `$calc`, regex (`$regex`/`$regml`/
  `$regsub`), hash tables (`/hadd`/`$hget`), file & INI I/O, `$mask`, `$duration`, `$base`, …
- Events / remotes: `on TEXT/ACTION/JOIN/PART/KICK/DIALOG/...` loaded from `.mrc` files and fired
  live, with `$nick`/`$chan`/`$target` context.
- **Dialogs**: `dialog` tables render as real Qt dialogs (`/dialog`, `/did`, `$did`, `on DIALOG`).
- **Picture windows**: `/window -p` + `/drawrect`/`/drawtext`/`/drawline`/`/drawdot`.
- Custom `@windows`, `/timer`.

Validated against real scripts — [NeonScript](https://github.com/jawshthedark/neonscript) (35 files,
17k lines, 1024 aliases + 457 handlers) loads fully, plus Anope/abaexp/ppfserv test scripts.

**Theming (Phase 3, started).** Switchable skins (View ▸ Theme): Default, Dark, OLED, and a **BeOS**
skin. Per-channel backgrounds/fonts and import/export are planned.

See `docs/screenshots/` for captures of each of the above.

## Building

Needs CMake 3.21+, Ninja, a C++20 compiler, and Qt 6.4+ (Widgets, Network, WebSockets).

```bash
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.3/gcc_64
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/gui/lurker-desktop
```

Without Qt, `-DLURKER_BUILD_GUI=OFF` builds and tests just the engine core.

Dev flags: `--connect-dev` (connect from `.secrets/dev-profile.json`), `--script=<file|dir>` (load
MSL), `--msl=<line>` (run MSL), `--theme=<id>`, `--screenshot=out.png[,delayMs]` (works with
`QT_QPA_PLATFORM=offscreen`).

## License

[MPL-2.0](LICENSE), same as Lurker.
