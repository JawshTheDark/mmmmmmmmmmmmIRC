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
links, nick/command tab-completion, input history. A clean-room **mIRC-style icon toolbar** (Connect,
Join, Channels list, Favourites, Options, Colors, Script Editor, DCC, Logs, About) and a full
**tree-based Options dialog** (Connect/IRC/DCC/Display/Colors/Sound/Mouse/Logging/Other) mirror
mIRC's layout; every setting persists.

**Scripting (Phase 2).** A real MSL interpreter (Qt-free core, `src/core/msl/`):

- Evaluation: `$identifiers`, `%variables`, `$N` params, `$+` / `$+()`, `[ ]` grouping, `$(text,N)`
  N-pass evaluation, custom identifiers (`$myalias(...).prop`).
- Control flow: `if`/`elseif`/`else` (braced, braceless, one-line and `|`-separated), `while` with
  `/break` & `/continue`, `/tokenize`, aliases (recursion-guarded), `/return` `/halt`.
- ~100 identifiers and ~50 commands: text/token/number/time, `$calc`, `$gettok` ranges (`N-M`),
  `$wildtok`/`$reptok`, case-insensitive `==`/`!=`, regex (`$regex`/`$regml`/`$regsub`), hash tables
  (`/hadd`/`$hget`), file & INI I/O, `$mask`, `$duration`, `$base`, …
- Events / remotes: `on TEXT/ACTION/JOIN/PART/KICK/DIALOG/...` loaded from `.mrc` files and fired
  live, with `$nick`/`$chan`/`$target` context.
- **Dialogs**: `dialog` tables render as real Qt dialogs (`/dialog`, `/did`, `$did`, `on DIALOG`).
- **Picture windows**: `/window -p` + `/drawrect`/`/drawtext`/`/drawline`/`/drawdot`.
- Custom `@windows`, `/timer`.

Validated against real scripts — [NeonScript](https://github.com/jawshthedark/neonscript) (35 files,
17k lines, 1024 aliases + 457 handlers) loads fully, plus Anope/abaexp/ppfserv test scripts.

**Theming (Phase 3, started).** Switchable skins (View ▸ Theme): Default, Dark, OLED, and a **BeOS**
skin. Configurable chat **font & size** (Options ▸ Display) apply live to every window, plus an
editable **16-colour mIRC palette** (Options ▸ Display ▸ Colors). Per-channel backgrounds and
theme import/export are planned.

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
MSL), `--msl=<line>` (run MSL), `--theme=<id>`, `--options[=Page]` / `--scripteditor` (open those
windows), `--export-icon=out.png[,size]`, `--screenshot=out.png[,delayMs]` (works with
`QT_QPA_PLATFORM=offscreen`).

## Releases & CI

Every push and PR is built and tested on **Linux and Windows** by GitHub Actions
([`.github/workflows/ci.yml`](.github/workflows/ci.yml)). Pushing a version tag cuts a release with
prebuilt binaries — a Linux **AppImage** and a Windows **zip** —
([`.github/workflows/release.yml`](.github/workflows/release.yml)):

```bash
git tag v0.1.0
git push origin v0.1.0
```

Artifacts are attached to the GitHub Release for the tag.

## License

[MPL-2.0](LICENSE), same as Lurker.
