# Clean-room log

Every area of this client is specified from public documentation or from black-box observation
of mIRC. This file records the sources used per area. If a source is not listed here, it was not
used. Disassembly or decompilation of mIRC is never a source; the earlier REA/Ghidra exploration
of `mirc.exe` recorded in this project's chat history is explicitly excluded.

| Area | Sources |
|---|---|
| IRC formatting codes (`src/core/IrcFormat`) | https://modern.ircdocs.horse/formatting ; Lurker `vue_client/src/utils/nickColor.ts` (MPL-2.0) for the 16-colour slot order |
| Raw IRC lines and tags (`src/core/IrcMessage`) | https://modern.ircdocs.horse ; https://ircv3.net/specs/extensions/message-tags |
| Slash commands (`src/core/CommandParser`) | Lurker `docs/CLIENT_PROTOCOL.md` §6 and §12; general IRC command names (RFC 2812) |
| Parity matrix (`tools/parity/data`) | names only, from the index of mIRC's help file (`mirc.hhk`, 7.72) and the public changelog https://www.mirc.com/versions.txt (7.73–7.85). The help text itself is consulted per work package and listed on that row below. |
| Lurker backends | Lurker `docs/CLIENT_PROTOCOL.md`, `docs/IRCV3.md`, `docs/OAUTH.md` |

Per work package (add a row when you start one):

| WP | Help pages / specs read | Oracle case sets |
|---|---|---|
| | | |
