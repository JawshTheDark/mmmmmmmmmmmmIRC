# lurker-desktop design

A native Linux desktop IRC client for [Lurker](https://github.com/amiantos/lurker), in the mIRC
tradition: MDI windows, treebar, switchbar, nicklists, and the **mIRC scripting language** so
existing scripts keep working. C++20 and Qt 6, MPL-2.0 like Lurker.

## Goals (from the owner)

1. Feature parity with mIRC, as far as it can be had on Linux. The yardstick is a parity matrix
   (see below), not a feeling.
2. Existing mIRC scripts run and behave the same.
3. Tailored to Lurker, reachable two ways: Lurker's built-in IRC bouncer, or its WebSocket/REST API.
4. Clean room: no mIRC code, assets or resources are copied.

## Clean-room rules

- Behavior is specified from **public documentation** (mIRC's published help and scripting docs,
  the IRC and IRCv3 specifications, community scripting references) and from **black-box
  observation** of the program's behavior.
- Nothing is taken from disassembly or decompilation of mIRC. No mIRC logo, icons, sounds, images,
  dialog layouts or resource strings are reused. Error and status messages are written fresh.
- Where documentation is ambiguous, the tie-breaker is a behavioral test: run a script in real mIRC
  (the owner's licensed copy, e.g. under Wine) and record what it does. Tests encode the observed
  behavior; they do not embed mIRC code.
- The UI is mIRC-inspired in structure, not a pixel copy.

## Layers

```
src/core/     Qt-free C++20. Pure, unit-tested.
              IrcFormat      formatting codes -> styled spans            (done)
              CommandParser  slash commands -> actions                   (done, becomes the
                                                                          built-in command table
                                                                          the script engine falls
                                                                          back to)
              IrcMessage     raw IRC line + IRCv3 tags parser/builder
              script/        mIRC scripting engine (lexer, parser, interpreter, identifiers,
                             events, aliases, popups, timers, hash tables, files, sockets API)
src/backend/  Backend interface + implementations (Qt Network)
              BouncerBackend  raw IRCv3 over TLS to Lurker's bouncer
              LurkerBackend   WebSocket + REST (history, read state, search, uploads, ...)
src/gui/      Qt Widgets: MDI workspace, treebar, switchbar, nicklist, toolbar, dialogs
```

`src/core` has no Qt dependency so it builds and is tested anywhere (`-DLURKER_BUILD_GUI=OFF`).

## Two backends, and why it matters for scripts

mIRC scripts are written against the **raw IRC event stream**: `on TEXT`, `on JOIN`, `raw 353:`,
`on SNOTICE`, `$ctcp`, numerics, server `$parms`. Fidelity depends on what the backend delivers.

| | Bouncer (raw IRCv3) | WebSocket/REST |
|---|---|---|
| What the client sees | The real IRC line stream, like any IRC client | Server-digested JSON events (`irc` frames) |
| `raw N:` / `on RAW`, `on SNOTICE`, `on UNOTICE`, `$parms`, server text | Yes, natively | No raw lines: events are synthesized, numerics not available |
| Lurker-only features: server history/search, read state, drafts, bookmarks, uploads, reactions UI, push | Partly (chathistory, soju extensions) | Yes |
| Best for | Script fidelity | Lurker features |

Decision: the script engine runs on a unified internal event model fed by a `Backend`. The
**bouncer backend is primary for script-driven use** because it is the only one that can honour
raw-event scripts. The WebSocket backend supplies Lurker-specific features, and the two can run
together (bouncer for IRC, WebSocket for history/read-state/uploads). Where a script needs
something only raw IRC provides and the user is on the WebSocket backend alone, the client says so
rather than pretending.

Both backends ride Lurker's documented protocols (`docs/CLIENT_PROTOCOL.md`, `docs/IRCV3.md`
in the Lurker repo).

## What "100% parity" can and cannot mean on Linux

Tracked as a checklist generated from the public mIRC command/identifier/event index, each row
`done | partial | planned | not-possible`, with a test per row.

Portable, in scope: the scripting language (variables, control flow, aliases, events, raw, popups,
timers, hash tables, binary variables, file/ini I/O, `$regex` via PCRE2, `/dialog`, sockets via
Qt), the full identifier and command set that does not depend on Windows, DCC chat/send/get and
file server, notify/ignore/user levels/address book, logging, perform, favorites, URL catcher,
sound events, themes and color schemes, the options dialogs.

Cannot be made identical, documented with the closest substitute:

- `$dll` / `/dll`: existing mIRC plugins are Win32 DLLs. A Linux build cannot load them.
  Substitute: a native plugin ABI, and optionally a Wine-hosted bridge later.
- `$com` / COM, ActiveX, DDE, Windows Registry, MS Agent (`/gload`, ...): Windows-only technologies.
  Scripts using them get a clear error; common registry/shell use cases get portable equivalents.
- `/run` of Windows programs: routed to `xdg-open` or Wine when configured.
- Pixel-exact native Win32 control look.

Everything in the "cannot" list is listed in the matrix so nothing is silently missing.

## Roadmap

The phased plan with work packages and exit gates is [PLAN.md](PLAN.md); progress is measured in
[PARITY.md](PARITY.md), generated from `tools/parity/data/*.tsv`.
