# Clean-room log

Every area of this client is specified from public documentation or from black-box observation
of mIRC. This file records the sources used per area. If a source is not listed here, it was not
used. Decompiled mIRC **code** — its functions, logic, data structures or control flow — is never
a source for any implementation here, and no mIRC strings, dialog layouts, icons, sounds or other
assets are reused.

Reverse-engineering note (2026-10-15, owner-directed): RE tooling (REA) was run once on
`mirc.exe` 7.85 purely to read its **static structural inventory** — the PE version field
(FileVersion 7.85.0.0, mIRC Co. Ltd.) and resource-type *counts* (125 RT_DIALOG, 2 RT_MENU, ~191
RT_STRING tables, 316 icons / 78 groups, 3 WAVE). These are facts, not expression. The resource
*payloads* (dialog layouts, menu text, string tables) were opaque to the inventory and were not
read; no procedure was decompiled into any implementation. The finding used downstream is only
the scale of mIRC's dialog surface, which informs *which* parity dialogs to build — built from the
public help file, not from mIRC's resources. The earlier REA/Ghidra exploration recorded in chat
history remains excluded as an implementation source.

| Area | Sources |
|---|---|
| IRC formatting codes (`src/core/IrcFormat`) | https://modern.ircdocs.horse/formatting ; Lurker `vue_client/src/utils/nickColor.ts` (MPL-2.0) for the 16-colour slot order |
| Raw IRC lines and tags (`src/core/IrcMessage`) | https://modern.ircdocs.horse ; https://ircv3.net/specs/extensions/message-tags |
| Slash commands (`src/core/CommandParser`) | Lurker `docs/CLIENT_PROTOCOL.md` §6 and §12; general IRC command names (RFC 2812) |
| IRC session state machine (`src/core/IrcSession`) | https://modern.ircdocs.horse (registration, numerics, CTCP, ISUPPORT); https://ircv3.net/specs/extensions/capability-negotiation and .../sasl-3.1 (CAP 302, SASL PLAIN); RFC 1459 §2.2 casemapping |
| Bouncer backend / attach protocol (`src/backend/BouncerBackend`) | Lurker `docs/IRCV3.md` (downstream caps), `docs/SELF_HOSTING.md` (bouncer attach, PASS/SASL credential forms, TLS cert pinning); soju bouncer-networks behaviour as documented there |
| Lurker WS/REST client (`src/backend/LurkerClient`) | Lurker `docs/CLIENT_PROTOCOL.md` §3 (auth/token mint), §4 (WS upgrade, snapshot burst, resume `?since`), §5 (data model), §6 (verbs), §7 (frames); `docs/OAUTH.md` for the future PKCE path |
| Parity matrix (`tools/parity/data`) | names only, from the index of mIRC's help file (`mirc.hhk`, 7.72) and the public changelog https://www.mirc.com/versions.txt (7.73–7.85). The help text itself is consulted per work package and listed on that row below. |
| Lurker backends | Lurker `docs/CLIENT_PROTOCOL.md`, `docs/IRCV3.md`, `docs/OAUTH.md` |

Per work package (add a row when you start one):

| WP | Help pages / specs read | Oracle case sets |
|---|---|---|
| | | |
