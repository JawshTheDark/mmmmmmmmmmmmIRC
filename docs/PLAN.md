# Plan: a cross-platform mIRC replica with Lurker interoperability

This is the execution plan for `lurker-desktop`. It is written so that an agent or a person can
pick up any work package (WP) cold. Read [DESIGN.md](DESIGN.md) for the architecture and the
clean-room rules first; this document says *what* to build, *in which order*, and *how we know
it is done*.

The live scoreboard is [PARITY.md](PARITY.md), generated from `tools/parity/data/*.tsv`.

## 0. Vision (from the owner)

An **exact replica of mIRC**, running on **both Linux and Windows**, with modern **interoperability**.
The owner's requirements, stated and non-negotiable:

1. **Exact mIRC replica** — look, feel and behaviour match mIRC 7.85 as closely as the host allows:
   MDI windows, treebar, switchbar, nicklists, menus, dialogs, the options tree, the Scripts Editor.
2. **Full MSL (mIRC Scripting Language)** — scripts, aliases, variables, events/remotes, raw, ctcp,
   popups/menus, timers, hash tables, binary variables, file and INI I/O, sockets, custom and picture
   windows, and **`/dialog`** dialogs. Existing mIRC scripts load and run unchanged. This is Phase 2
   and is the heart of the project.
3. **Total theming** — users restyle everything: window backgrounds (image or colour, global and
   per-window/per-channel), fonts and text size (global and per-window), every UI/message colour,
   nick colouring, line spacing, and script-rendered graphics (picture windows). Themes import and
   export. Light/dark. This runs through Phase 3.6 and the picture-window work in Phase 2.9.
4. **Cross-platform: Linux and Windows** (Qt 6 builds both). The parity matrix's `not-possible`
   rows are **Linux-only** caveats — on Windows, `$dll`/`/dll`, COM/ActiveX (`$com`), DDE and MS
   Agent are implementable natively, so those rows are `done` on Windows and `not-possible`/
   `substitute` on Linux. The matrix carries a per-platform status where they differ. macOS is a
   later nice-to-have (Qt supports it) but not a stated requirement.
5. **Interoperability** — speaks to Lurker (bouncer *and* WebSocket/REST), to soju/ZNC bouncers, and
   directly to ordinary IRC networks; reads and writes mIRC's own on-disk files so an mIRC install
   moves over as-is, and exports/imports so users move between mIRC and this freely.
6. **Clean room** — see §2 and DESIGN.md. Nothing derived from mIRC's binary.

## 1. Definition of done

"100% parity" means every item mIRC 7.85 documents is `done` or `substitute` in the matrix,
except items marked `not-possible` (Windows-only technology, each with a written reason — see §6).

    parity % = (done + substitute) / (total − not-possible − n/a)

The matrix is built from mIRC's own help index plus the public changelog for 7.73–7.85. Today it
holds roughly **256 commands, 420 identifiers, 75+ events (plus raw numerics and CTCP), 13 syntax
rules and 360+ feature/option/UI topics**. A row is `done` only when:

1. it is implemented from public documentation (help file, changelog, IRC/IRCv3 specs) or from
   black-box observation of mIRC (§5.4), never from mIRC's binary;
2. it has a test (unit test or script fixture) named in the row's `test` column;
3. where the documentation was ambiguous, the behaviour was confirmed against the oracle (§5.4).

Partial implementations are `partial` with the gap written in `notes`. Nothing is silently skipped.

On top of parity, the client must be **tailored to Lurker**: it connects through Lurker's bouncer
(raw IRCv3) and through Lurker's WebSocket/REST API, and exposes the Lurker-only features (history,
search, read state, uploads, reactions, replies, typing, presence) — §4, Phase 4.

## 2. Inputs

| What | Where (this machine) | Use |
|---|---|---|
| Target version | mIRC **7.85** (`/home/jawsh/mirc.exe`, user's licensed copy) | behaviour oracle under Wine (§5.4) |
| Help file 7.72 | `/mnt/wd2tb/mIRC/mirc.chm` → `7z x mirc.chm -o<dir>`; index is `mirc.hhk` | the matrix; per-topic specs |
| Help 7.85 (online) | https://www.mirc.com/help/html/ | same, current version |
| Changelog | https://www.mirc.com/versions.txt | 7.73–7.85 additions (already folded into the matrix) |
| IRC / IRCv3 specs | https://modern.ircdocs.horse, https://ircv3.net/irc/ | protocol behaviour |
| Lurker source + docs | `~/Desktop/Projects/lurker-upstream` (clone of github.com/amiantos/lurker), esp. `docs/CLIENT_PROTOCOL.md`, `docs/IRCV3.md` | both backends |
| Script corpus A | `/mnt/wd2tb/mIRC/scripts/` (user's own, 5 files) | load/run fidelity |
| Script corpus B | `/mnt/md0/projects/war-gen3-orig/aowbackup3/` ("AOW" game bot, 52 files, 21.6k lines; INI-heavy, multi-server, timers, /filter) | load/run fidelity |
| Script corpus C | `/mnt/md0/jawsh-home-backup/data/Desktop/Invincible_Script_by_Xtry-master/` (116 files, 36k lines; dialogs ×5.7k, custom/picture windows, sockets, sounds, Win32 DLLs) | load fidelity, dialog/window stress test, DLL gap |
| Wine | `/usr/bin/wine` | run the oracle |

Corpora B and C are third-party scripts kept outside the repository; tests reference them by path
and skip when absent. Never copy them in.

**Clean-room boundary.** The REA/Ghidra analysis of `mirc.exe` done earlier in this project's
history was exploratory and is not an input. No fact derived from disassembly goes into code,
tests or specs. Record every source consulted for a WP in `docs/CLEANROOM.md`.

## 3. Architecture the plan assumes

- `src/core/` is Qt-free C++20 and holds the scripting engine and all protocol-independent
  logic. It talks to the outside world through a **`Host` interface** (timers, clock, files,
  sockets, windows, dialogs, sound, clipboard, process launch, IRC connections). `src/gui/` and
  `src/backend/` implement `Host` with Qt; tests implement a fake. This is what keeps the engine
  testable without a display and keeps the Qt dependency out of the language.
- Two backends behind one `Backend` interface feeding one internal event stream:
  **BouncerBackend** (raw IRCv3 over TLS to Lurker's bouncer; primary for scripts because scripts
  see raw lines and numerics) and **LurkerBackend** (WebSocket + REST; Lurker-only features). They
  can run together on the same account.
- mIRC-compatible on-disk layout (`mirc.ini`, `servers.ini`, `aliases.ini`, `popups.ini`,
  `remote.ini`, `users.ini`, `vars.ini`, `*.mrc`, `channels.ini`, `urls.ini`, address book) so an
  existing mIRC folder imports as-is.
- Script engine = lexer → parser (script files, aliases, events, popups, dialog tables) →
  evaluator with mIRC's evaluation model → registries of built-in commands and identifiers →
  runtime state (windows, nicklists, IAL, timers, sockets, hash tables, binary variables, files,
  multi-server connection ids).

## 4. Phases and work packages

Order matters: each phase's exit gate is the entry condition of the next. Within a phase, WPs are
roughly ordered by corpus usage (what real scripts need most comes first).

### Phase 0 — Foundation ✅ (done)

Core libs with tests (`IrcFormat`, `CommandParser`, `IrcMessage`), Qt shell (MDI, treebar,
switchbar, status window), build/test harness, `--screenshot` flag, parity tooling, this plan.

### Phase 1 — Connect and chat (a daily-drivable client)

Exit gate: the owner uses it daily against their Lurker server through **both** backends; an
imported mIRC folder shows the same servers, channels and options.

Progress (2026-10-14): **1.1 partial** (internal event model `core/IrcEvent` + `Backend` via
`BouncerBackend`; the formal `Host` interface is still to be extracted), **1.2 done** (bouncer
backend verified live against chat.irc.so:1025 — SASL, CAP incl. CAP NEW, ISUPPORT, channels/
members, reconnect, cert pinning), **1.3 done** (status/channel/query ChatViews with nicklist,
topic bar, per-window input, send via the command parser; tab-completion of nicks and commands,
input history on Up/Down, 5000-line scrollback cap, clickable URL hotlinks — completion logic in
`core/Completion` with tests). Remaining 1.3 nicety: full mIRC key-binding set (Ctrl+K colour
dialog, Alt+N window switch, etc.) — deferred to Phase 3.4.

**1.4 done, integrated into the GUI.** Auth: the Lurker WS/REST protocol needs a *minted session
token* (`POST /api/auth/login/token`, the account's web password) — the API token is bouncer + `/mcp`
only. `backend/LurkerClient` does config, token mint, networks, the WS connect, the
snapshot/backlog/history/irc/read-state/send-result frame dispatch, the resume cursor (system-buffer
ids excluded), and send/history/mark-read verbs (`lurker-ws-probe` verifies it: all 4 networks / 34
channels in one snapshot).

**Both backends now render through one GUI** via `backend/ChatBackend` (abstract) +
`LurkerChatBackend` and `BouncerChatBackend` adapters. MainWindow is **multi-network**: buffers
keyed by (networkId, folded target), a treebar that groups channels under their network (named from
REST), live nicklists and member counts, new windows opening minimized so dozens of channels don't
bury the workspace. Verified live: the Lurker path shows Libera.Chat / MansionNET / Rizon / irc.so
with every channel at once. ConnectDialog offers both backends.

**Partial 1.5 (connection manager):** remaining — saved networks + keychain storage (Secret Service),
reconnect/resume for the WS path, OAuth PKCE (no-password distribution path). 1.6 (options bound to
mirc.ini) and 1.7 (import an mIRC folder) not started. Also deferred from 1.3: full mIRC key
bindings; and scroll-up history fetch (WS `history before` / bouncer `CHATHISTORY`).

| WP | Scope | Deliverables | Exit criteria |
|---|---|---|---|
| 1.1 Host + Backend interfaces | `core/Host.h`, `core/Backend.h`, internal event model (one struct per IRC event with raw line attached when available) | headers, fake Host for tests | engine-side code compiles with no Qt |
| 1.2 Bouncer backend | QSslSocket; CAP LS 302; SASL PLAIN + EXTERNAL (CertFP); `soju.im/bouncer-networks` (list networks, one connection per network); server-time, message-tags, echo-message, batch, chathistory, labeled-response, multi-prefix, userhost-in-names, away-notify, account-notify, extended-join, chghost, setname, invite-notify, draft/read-marker, typing, +draft/react/reply, cap-notify, sts; PING/PONG; reconnect/backoff; ISUPPORT (CASEMAPPING, PREFIX, CHANMODES, CHANTYPES, STATUSMSG, NETWORK, …); nicklist/IAL state; numerics routed to status | `src/backend/Bouncer*`; protocol tests against a fake ircd | connects to Lurker's bouncer, joins, talks, survives reconnect; raw numerics reach the status window |
| 1.3 Core windows | Status (per network), Channel (nicklist + topic bar), Query; input box (history, tab-completion, mIRC key bindings); timestamps; formatting rendering; URL detection/hotlinks; scrollback/buffer limits; window system menu; treebar/switchbar behaviour; Alt+N, Ctrl+Tab | `src/gui/*Window` | all basic IRC use works by mouse and keyboard |
| 1.4 Lurker WebSocket/REST backend | `GET /api/config`; token mint; OAuth PKCE (QtNetworkAuth); `GET /api/networks`; WS connect with Bearer; snapshot burst, shells, `backlog-complete`; `history` (latest/before, token discipline); `send/action/notice/raw` with `clientId` acks; read-state + `mark-read`; resume `?since` with the cursor rules (system buffer excluded, ephemeral events excluded, `reset:true`); presence; typing; reactions/replies display | `src/backend/Lurker*`; tests from recorded frames | sign-in, backlog, send, resume all work; §9 rules of CLIENT_PROTOCOL.md pass |
| 1.5 Connection manager | server/network list (mIRC `servers.ini` model + Lurker networks via REST/BOUNCER), connect dialog, credential storage (Secret Service D-Bus with encrypted-file fallback), multi-server (`/server -m`, cids) | dialogs, `ConnectionManager` | user can add/edit/remove networks and connect to several at once |
| 1.6 Everyday options | Options dialog skeleton with: Connect (servers, options, identd, firewall/proxy, local info), IRC (options, messages, catcher, flood, logging, highlight, perform), Display (options, colors, fonts), Sounds (event sounds), Other (keys, confirm); logging (mIRC log format/dir); notify (MONITOR), ignore, highlight, nick colours; tray icon + flashing | options model bound to `mirc.ini` keys | round-trips an imported `mirc.ini` |
| 1.7 Import | import an existing mIRC folder: `mirc.ini`, `servers.ini`, scripts list, `users.ini`, `vars.ini`, `popups.ini`, `aliases.ini`, address book, `channels.ini`, `urls.ini`, logs dir | `tools/import` + first-run wizard | corpora A and B import without error |

### Phase 2 — Scripting engine (mIRC scripts run unchanged)

Exit gate: corpora A and B **load and run** with identical observable behaviour on a test server;
corpus C **loads** (parses, dialogs render, windows open) with only DLL-backed features reporting
`not-possible`; all `syntax` rows done; ≥90% of commands/identifiers/events done.

| WP | Scope | Exit criteria |
|---|---|---|
| 2.1 Language core | lexer (line-oriented; `{ }` blocks; `\|` separators; `;` and `/* */` comments; `$&` continuation); parser for script files (`alias [-l]`, `on <level>:<EVENT>:<matchtext>:<target>:{}`, `raw N:`, `ctcp`, `#group on/off`, `menu`, `dialog` tables); evaluator: identifiers left-to-right with mIRC spacing rules, `$+`, `[ ]` priority, `$eval(x,N)`, `$!`, `$$`, `$(…)`, `%var` substitution, `$1 $2 $1- $N-`, `$0`, `/tokenize`; command prefixes `/ // . !`; `if/elseif/else` with the full operator set (`== === != < > <= >= // \\ isin isincs iswm iswmcs isnum isletter isalpha isalnum islower isupper ison isop isvoice ishop isowner isadmin isreg isignore isprotect isnotify ischan isaop isavoice isban isquiet isexcept isinvite`), `&&`/`\|\|`, nested parentheses and their spacing quirks; `while`/`break`/`continue`; `goto :label`; `return`/`halt`/`haltdef`; `/var` locals; global `/set /unset /inc /dec` with switches; `$null/$true/$false/$cr/$lf/$crlf/$tab/$chr/$asc`; error semantics (`$error`, `/reseterror`, `on ERROR`); script loading (`/load -rs/-a/-u`, `/unload`, `/reload`, `[rfiles]/[afiles]`); `$script/$scriptdir/$alias/$isalias/$isid/$event/$ulevel/$clevel/$dlevel` | `tests/script/*.mrc` fixtures with expected transcripts pass; every `syntax` row done |
| 2.2 Events and IRC state | every `on` event with level/matchtext/target semantics and `$nick $chan $target $address $site $wildsite $fulladdress $maddress $msg $rawmsg $numeric $1-`; `raw` numerics; `ctcp` events and `$ctcp`; halting default text; event order across scripts; user levels (`/auser /guser /ruser /ulist`, `users.ini`); IAL (`$ial $ialchan /ialmark /ialclear /ialfill`); `$chan() $nick() $comchan() $snick() $opnick() $vnick() $hnick() $nvnick() $modefirst/$modelast $prefix $chantypes $chanmodes $usermode $nickmode $topic $ibl $iel $iil $banmask $mask()`; `$server $network $port $me $cid $activecid $status $lag $ssl*`; `/scon /scid $scon()` multi-server | corpus B's event handlers fire identically to mIRC on a replayed session |
| 2.3 Text, tokens, time, math | all `text and number`, `token`, `time and date` identifier pages: `$len $mid $left $right $pos $poscs $replace $replacecs $remove $removecs $upper $lower $str $strip $iif $calc $rand $r $round $int $floor $ceil $abs $sqrt $log $sin… $and $or $xor $not $biton $bitoff $isbit $base $bits $bigfloat $gettok $addtok $deltok $findtok $instok $istok $matchtok $numtok $puttok $remtok $reptok $sorttok $wildtok $ctime $asctime $date $time $adate $fulldate $day $dow $duration $gmt $timezone $ticks $uptime $idle $regex $regsub $regsubex $regml $regerrstr $hash $md5 $sha1 $sha256 $sha512 $crc $hmac $hotp $totp $encode $decode $utfencode $utfdecode $ansi2mirc $qt $noqt $cb /clipboard /tokenize /filter /sorttok …` with PCRE2-compatible regex (QRegularExpression in the host, PCRE2 directly in core) | identifier rows done with unit tests; oracle-confirmed edge cases recorded |
| 2.4 Files, INI, hash tables, binary variables | `/write $read $lines /fopen /fclose /fseek /fread /fwrite /flist $fopen $feof $ferr /mkdir /rmdir /remove /rename /copy $exists $isfile $isdir $file() $findfile $finddir $nofile $nopath $mircdir $scriptdir $logdir $getdir $sysdir $env /loadbuf /savebuf /filter $ini $readini /writeini /remini /copyini /hmake /hfree /hadd /hdel /hinc /hdec /hload /hsave $hget $hfind /bset /bunset /bcopy /bread /bwrite /breplace /btrunc $bvar $bfind $compress $decompress $zip $urlget` | corpus B (INI-heavy game) runs its economy loop identically |
| 2.5 Timers and scheduling | `/timer[name]` with every switch (`-c -d -e -h -i -m -o -p -r -z` …), `$timer()`, `$ltimer`, `/timers`, `on *:TIMER`? (n/a), `$ctimer`, millisecond timers via Host | timer fixtures pass with fake clock |
| 2.6 Custom windows | `/window` with all switches (`-a -b -c -d -e -h -i -k -l -n -o -p -r -s -w -x -z`, fonts, colours, sizes, icons), listbox/sidebar, `/aline /dline /rline /iline /sline /cline /clear /echo -w`, `$window() $line() $sline() $fline() $wrap $width $height`, `menu @win`, `/titlebar`, `/renwin`, desktop windows (`-d` → top-level), `on OPEN/CLOSE/ACTIVE`, `$activewid` | corpus C's windows open and populate |
| 2.7 Dialogs | dialog tables → Qt widgets with dialog-unit mapping; every control (`text edit button check radio box list combo icon tab menu item link scroll`), styles (`multi, read, hsbar, vsbar, autohs, autovs, number, pass, return, ok, cancel, default, flat, push, 3state, size, sort, extsel, multsel, check, radio, bottom, top, noformat, nowrap, center, right, result, disable, hide, group`), `/dialog` (`-m -d -h -s -t -r -o -x -k -b -a -i -g -n -c -e -z`), `/did` (all), `$did()` (all properties), `$dialog()` (`.x .y .w .h .cw .ch .title .modal .table .ok .cancel .result .active .hwnd .tab .mapw .maph`), `on DIALOG` (`init sclick dclick edit menu close rclick drop mouse uclick leave scroll …`), `$dname $devent $did $dlevel`, `$input()`/`$?`/`$?!` prompts, `/dialog -m` modal loop | all 314 `on DIALOG` handlers in corpus C compile; its main dialogs render and respond |
| 2.8 Sockets and network identifiers | `/sockopen (-e ssl) /socklisten /sockaccept /sockudp /sockread (-f -n, &binvar) /sockwrite (-n -t) /sockclose /sockmark /sockrename /sockpause`, `$sock()` properties, `$sockerr $sockname $sockbr`, `on SOCK*`, `on UDPREAD`, `/dns on DNS $dns() $iptype $longip $ip $host $bindip /localinfo`, `$urlget`, identd (`$identd`) | corpus C's socket scripts (31 `SOCKOPEN` handlers) talk to a local test server |
| 2.9 Picture windows | `/window -p`, `/drawpic /drawtext /drawrect /drawline /drawdot /drawfill /drawcopy /drawscroll /drawsave /drawreplace /drawrot /drawsize`, `$pic() $rgb $color $inrect $inellipse $inpoly $inroundrect $width $height`, mouse/`sclick`/`dclick`/`rclick`/`uclick`/`drop`/`leave` hooks, `$mouse` | corpus C's graph/bars windows draw |
| 2.10 Sound and media | `/splay` (wav/mp3/ogg/flac via QtMultimedia; `-q -p -s -c -w -m -l`…), `/sound` (CTCP SOUND), `/beep`, `/vol`, `$sound() $vol $insong $inwave $inmp3 $inmidi $findfile(mp3)`, `on MP3END/WAVEEND/MIDIEND/PLAYEND/NOSOUND`, sound requests options | corpus C's 126 `/splay` calls work; MIDI is `substitute` until Phase 6 |
| 2.11 Popups and menus | `popups.ini` sections (status, channel, query, nicklist, menubar) and `menu` blocks in scripts: nesting (`.`), separators, `$submenu`, `$1`-nick lists, `$snick`, dynamic items; `/popup`? n/a; keyboard accelerators | corpus A's popups.ini (93 lines) renders identically |
| 2.12 DCC and file server scripting | `/dcc send/get/chat/fserve/trust/ignore/nick`, passive DCC, resume, DCC server, `$send() $get() $chat() $fserve() $dccport $sendfile?`, `on CHAT/SERV/FILESENT/FILERCVD/SENDFAIL/GETFAIL/DCCSERVER`, `/msg =nick`, `$dccignore`, folders/file-type options. **DCC is routed through Lurker, not peer-to-peer** (see §DCC below): a scripted `/dcc send` uploads via the bouncer's `soju.im/FILEHOST` / Lurker's `/api/dcc`, and an incoming transfer is pulled from Lurker; the scripting surface (identifiers, events, progress) is preserved over that transport. A true peer-to-peer fallback is a later option, off by default. | corpus B's `/dcc` paths (20 uses) and corpus C's fserve load; a scripted send/get round-trips through Lurker |
| 2.13 The long tail | everything still `planned` after 2.1–2.12: `/clipboard $cb /tip $tip /tray /toolbar $toolbar /font /color (colours model) /background /flash /showmirc /titlebar /echo (every switch) /linesep /timestamp $timestamp /editbox $editbox on INPUT/TABCOMP/CHAR/KEYDOWN/KEYUP $keyval $keychar /hotlink on HOTLINK /abook $abook /notify $notify /ignore $ignore /protect /ajinvite /ban masks $banmask /ban -a /uwho $uwho? /whois $whois? /url $url $urlget /log $log /perform /anick /emailaddr /fullname /nick /away $away $awaymsg $awaytime /exit /load /unload $appstate $appactive /speak (substitute) /registry (substitute)…` | matrix: commands/identifiers/events ≥ 99% done or not-possible |

### Phase 3 — Full UI and options parity

Exit gate: every `features` row done/substitute/not-possible; a user who knows mIRC finds every
dialog, menu and key where they expect it.

| WP | Scope |
|---|---|
| 3.1 Scripts Editor | tabbed editor (Aliases, Popups, Remotes, Users, Variables), file list/menu, syntax colouring, line numbers, bracket matching, find/replace with Count, listing, `-rs` reload semantics |
| 3.2 Options dialog, complete | every page and control of mIRC's Options tree (Connect, IRC, DCC, Display, Sounds, Mouse, Other) bound to `mirc.ini`; Colors dialog with schemes; Fonts; Lock; Confirm; Tray; Toolbar customisation |
| 3.3 Windows and menus | File/View/Favorites/Tools/Commands/Window/Help menus; window system menu (logging, timestamps, font, colours, beep, hide, …); Channel Central; Channels List window; Favorites/channels folder; Notify window; URL catcher window; Address Book (nicks, notify, control lists, nick colours); DCC transfer windows and status; Finger?; Link/Hotlink; Full screen; Arrange/cascade/tile; Desktop windows |
| 3.4 Keys, mouse, drag/drop, copy/paste | every `key_combinations` row; editbox behaviour (multi-line, Ctrl+K colour dialog, Ctrl+B/U/I/R/O codes, history, auto-complete, nick completion with suffix, Ctrl+Enter); nicklist/switchbar mouse options; drag/drop files to DCC send; copy/paste options |
| 3.5 Identd, proxy, SSL | identd server; SOCKS4/5 and HTTP proxy; TLS options, certificate warnings/accept dialog, CertFP management, SASL methods incl. SCRAM-SHA-256 and ECDSA-NIST256P-CHALLENGE |
| 3.6 Theming (total) | A full theme engine — the owner requires users restyle everything. mIRC colour-scheme model (all 16 palette slots + every UI element colour); fonts and **text size** global and per-window/per-channel; **window backgrounds** as solid colour or image, global and per-window/per-channel (`/background` and the options); line spacing; nick colouring; message-density and bubble/compact layouts; script-rendered graphics (picture windows, §2.9). Themes are a saved document that **imports and exports**; live preview. **Shipped skins:** a classic mIRC-like default, light/dark/OLED, and a **BeOS skin** (owner request — yellow tab-style title bars, BeOS grey, the classic chrome), all drawn fresh with no copied assets. A skin is a full QStyle/stylesheet + palette + chrome bundle, not just colours. |

### Phase 4 — Lurker tailoring

Exit gate: everything Lurker offers that mIRC never had is reachable without leaving the client.

| WP | Scope |
|---|---|
| 4.1 History and search | server history (`chathistory` on the bouncer; `history` frames on WS) with scroll-up loading; `GET /api/search` window; highlights and bookmarks lists |
| 4.2 Sync | read state (`read-state`, `mark-read`, draft/read-marker), drafts, favorites, pins, nick notes, settings registry editor (every advertised setting by type) |
| 4.3 Rich IRCv3 UI | reactions (+draft/react) under lines, replies with quoted parent, typing indicators, peer presence in queries, multiline messages, `account-tag`/`chghost` rendering, link/media previews when the server has `linkPreviews` |
| 4.4 Uploads and DCC through Lurker | paste/drag image → `POST /api/uploads` with progress token; size/dimension limits from the snapshot. **DCC proxied through Lurker**, configurable in Options: outgoing files go through the bouncer's `soju.im/FILEHOST` (attach → link) or Lurker's `/api/dcc` so transfers work behind NAT and across devices without opening listening ports; incoming DCC offers are accepted and fetched from Lurker; progress via `dcc-transfer` frames / `upload-progress`. The local save/send directories are client settings. This is the transport the scripted `/dcc` surface (WP 2.12) runs on. |
| 4.5 Notifications | `notify` flag → desktop notifications (freedesktop), sounds, tray; no push |
| 4.6 Account and admin | sign-in flows (self-hosted token, OAuth PKCE, hosted lurker.chat), paused-account state, `account-state`, network CRUD via REST with Lurker-only fields (SASL, CertFP generate/import/export, autoconnect, connect commands) |
| 4.7 Optional: E2E (RPE2E) | per-channel opt-in, key export/import, lock icon, `FLAG_SECURE`-style no-logging |

### Phase 5 — Audit, hardening, release

| WP | Scope | Exit criteria |
|---|---|---|
| 5.1 Matrix closure | walk every `partial`/`planned` row; write the test or the `not-possible` reason | PARITY.md total = 100% |
| 5.2 Differential sweep | run the oracle over the whole identifier/command surface with generated cases; diff; fix | diff is empty or each difference is documented as intentional |
| 5.3 Corpus runs | A and B: run; C: load + exercise dialogs/windows; record transcripts as regression fixtures | green in CI |
| 5.4 Performance | 2k-user channels, 100k-line scrollback, 500 timers, 50 sockets, 10 picture windows at 30 fps | budgets met on the owner's machine |
| 5.5 Packaging and CI | GitHub Actions building **both platforms**: Linux (AppImage + Flatpak + .deb) and Windows (installer + portable .zip); run tests and parity `--check` on each; update check | installable artefacts for Linux and Windows from CI |
| 5.6 Docs | user guide (differences from mIRC, Lurker setup), script-author guide (what differs on Linux), CLEANROOM.md complete | published with the release |

### Phase 6 — Closing the Linux gaps (native Linux equivalents)

The Linux analogues for mIRC's Windows-only surface. These make new Linux scripts/plugins first-class;
existing Windows binaries (`.dll`, `.acs`) still need the Wine bridge or a rebuild.

| WP | Scope |
|---|---|
| 6.1 Native `.so` plugin ABI (`$dll`/`/dll` on Linux) | `dlopen`/`dlsym` a shared object and call a plugin ABI mirroring mIRC's `int fn(mWnd, aWnd, char* data, char* parms, int show, int nopause)` (HWNDs become opaque handles; `data` is the in/out text buffer). Documented header so authors port a DLL by recompiling. |
| 6.2 Wine DLL bridge | run an **existing Windows** mIRC DLL in a tiny Win32 host under Wine, marshalling the call over a pipe. Works for compute DLLs; GUI DLLs that reach into mIRC's own HWNDs (mdx.dll, dialog.mdx) can't be bridged. |
| 6.3 D-Bus bridge (`$com`/`$dde` on Linux) | expose the client as a session **D-Bus** service (send it commands, read identifiers — the DDE "talk to mIRC" use case) and let scripts *call* D-Bus services and MPRIS players (the COM "automate another app / OS service" use case). Not a drop-in for automating Word-style COM objects; it maps the category. |
| 6.4 TTS + avatar (`agent` on Linux) | MS Agent is discontinued even on Windows and its `.acs` characters can't be reused. Provide the scriptable surface (the `/g*` commands and `on AGENT`) backed by **speech-dispatcher** TTS and an optional simple sprite/avatar window; otherwise a clear error. |
| 6.5 MIDI | optional FluidSynth backend so `$inmidi`/MIDI `/splay` become `done` on Linux |
| 6.6 Speech recognition | best-effort via a Linux STT engine (Vosk) for `on VCMD`, or error |

## 5. Testing strategy

1. **Unit tests** (`tests/test_*.cpp`, no framework): every core function.
2. **Script fixtures** (`tests/script/<name>.mrc` + `<name>.expected`): a script plus the
   transcript it must produce under the fake Host (fake clock, fake IRC lines fed from
   `<name>.irc`). This is the main vehicle for Phase 2; each matrix row's `test` column names one.
3. **Backend tests**: fake ircd for the bouncer; recorded WS frames for the Lurker backend.
4. **The oracle (differential testing).** Real mIRC 7.85 under Wine is the ground truth for
   anything the documentation leaves open. `tools/oracle/` holds `probe.mrc`, a script *of ours*
   that runs each line of `cases.txt` inside mIRC and appends outputs to `results.txt`; the same
   cases run in our engine and the two files are diffed. Setup: `WINEPREFIX=~/.wine-mirc wine
   /home/jawsh/mirc.exe` (the trial/registration dialog must be clicked once per start; a display
   or Xvfb is needed). Only observed behaviour is recorded — never anything from the binary.
5. **Corpus tests**: load-only for all three corpora on every CI run (paths are machine-local;
   skipped when absent); run-level checks for A and B in Phase 5.
6. **GUI checks**: `--screenshot` + `QT_QPA_PLATFORM=offscreen` for layout regressions.

## 6. Platform differences (cross-platform: Linux + Windows)

The goal is a build on **both Linux and Windows**. Qt 6 covers the UI, the scripting engine and the
backends on both. A handful of MSL features wrap Windows-only OS technology: those are **`done`
natively on Windows** and `not-possible`/`substitute` on Linux. The parity matrix records a
per-platform status (`status_win` / `status_linux`) wherever they differ, so "100%" is evaluated per
platform and nothing is hidden.

| Area | Windows | Linux |
|---|---|---|
| `$dll` / `/dll` / `/drawdll` / `$dllcall` | **done** — load the Win32 DLL and call its `int fn(HWND,HWND,char* data,char* parms,BOOL,BOOL)` directly | error naming the DLL; Phase 6 Wine bridge for compute-only DLLs; native `.so` plugin ABI |
| COM / ActiveX (`$com*`, `/com*`) | **done** — native COM | error; no general equivalent |
| DDE (`/dde*`, `$dde*`) | **done** — native DDE | error (or a local-IPC shim for same-app DDE) |
| Microsoft Agent (`/g*`, `$agent*`, `on AGENT`) | **substitute** — MS Agent is gone even on Windows; offer a modern TTS avatar or error | error; options page hidden |
| Speech (`/speak`, `$speak`) | **done** — SAPI | **substitute**: speech-dispatcher |
| Speech recognition (`/vc*`, `on VCMD`) | **done** — SAPI | error |
| `/run` | **done** — ShellExecute | **substitute**: `xdg-open`/exec; Wine when configured |
| `$os`, `$sysdir`, `$shortfn`, `$longfn`, registry | **done** — real values / registry | **substitute** values (XDG dirs; INI-backed registry store), documented |
| MIDI playback | **done** — system synth | **substitute** until 6.2 (FluidSynth) |
| Native control pixels | Win32-native where it helps | mIRC-like Qt look; dialog units mapped the Windows way so script dialogs keep their layout |

Phase 6 (Linux) narrows the Linux gaps; on Windows most of this column is simply implemented. CI
builds and tests both platforms (§5.5).

## 7. Risks

| Risk | Mitigation |
|---|---|
| mIRC's evaluation quirks (spacing, commas, `$+`, bracket order) are under-documented | oracle-driven fixtures early in 2.1; keep a quirk log in `docs/spec/evaluation.md` |
| Scope: ~1,100 matrix rows | strict corpus-usage ordering; `partial` is allowed and visible; no row without a test |
| Lurker's bouncer may not expose every raw numeric a script expects | measure in 1.2; document gaps; WS backend never pretends to deliver raw lines |
| Corpus C depends on DLLs for its UI | it is a load/parse/dialog stress test, not a run target |
| Wine oracle automation (trial dialog, display) | one-time manual setup; cases are batch-run; results are cached in the repo as fixtures |

## 8. The work loop (per WP)

1. Read the help page(s) for the rows in scope; write `docs/spec/<area>.md` in your own words
   (behaviour, switches, edge cases, open questions). Add the sources to `docs/CLEANROOM.md`.
2. Resolve open questions with the oracle; record cases and results under `tests/oracle/`.
3. Implement in `src/core` behind `Host`; add unit tests and script fixtures.
4. Wire the Qt side; take a screenshot for anything visual.
5. Set each row's `status`, `test`, `notes` in `tools/parity/data/*.tsv`; run
   `python3 tools/parity/render.py`; commit the TSVs and PARITY.md with the code.
6. Run the full test suite and the corpus load tests before every commit.
