#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""Build the parity matrix data files from a mIRC help index.

mIRC's help file (mirc.chm, part of every mIRC installation) contains an HTML
Help index (mirc.hhk) listing every documented command, identifier, event and
feature topic. This script turns that index into the TSV files under
tools/parity/data/, which track how far this project has got with each item.

The help file itself is not part of this repository. Extract it from your own
mIRC installation (`7z x mirc.chm -o<dir>`) and point this script at the .hhk.

Re-running the import keeps the status/notes/test columns already recorded for
a name, so progress is never lost; new names are added as `planned`.

Usage:
  import_help_index.py <mirc.hhk> [--corpus usage.json ...]

`--corpus` files are produced by scan_corpus.py and add a `corpus_uses` column
(how often real scripts use each item), which doubles as a priority order.
"""
import argparse
import collections
import csv
import html
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
STATUSES = ("planned", "partial", "done", "substitute", "not-possible", "n/a")

SYNTAX_KEYS = {"/", "$", "$!", "$$", "$&", "$(...)", "$+", "$?", "$?!", "$0", "$1-", "#", "!=", "-", "//"}

# Windows-only technology that a Linux build cannot reproduce. Everything here
# needs a written justification in the notes column; see docs/PLAN.md.
NOT_POSSIBLE = {
    "command": {
        "/comopen": "COM/ActiveX", "/comclose": "COM/ActiveX", "/comlist": "COM/ActiveX", "/comreg": "COM/ActiveX",
        "/dll": "Win32 DLL plugins (see Phase 6 Wine bridge)", "/drawdll": "Win32 DLL plugins",
        "/dde": "Windows DDE", "/ddeserver": "Windows DDE",
        "/gload": "Microsoft Agent", "/gtalk": "Microsoft Agent", "/gplay": "Microsoft Agent", "/gpoint": "Microsoft Agent",
        "/gsize": "Microsoft Agent", "/gmove": "Microsoft Agent", "/gshow": "Microsoft Agent", "/ghide": "Microsoft Agent",
        "/gqreq": "Microsoft Agent", "/gstop": "Microsoft Agent", "/gunload": "Microsoft Agent", "/gopts": "Microsoft Agent",
        "/vcadd": "Windows speech recognition", "/vcrem": "Windows speech recognition", "/vcmd": "Windows speech recognition",
    },
    "identifier": {
        "$com": "COM/ActiveX", "$comcall": "COM/ActiveX", "$comerr": "COM/ActiveX", "$comval": "COM/ActiveX",
        "$dde": "Windows DDE", "$ddename": "Windows DDE",
        "$dll": "Win32 DLL plugins (see Phase 6 Wine bridge)", "$dllcall": "Win32 DLL plugins",
        "$agent": "Microsoft Agent", "$agentname": "Microsoft Agent", "$agentstat": "Microsoft Agent", "$agentver": "Microsoft Agent",
        "$vcmd": "Windows speech recognition", "$vcmdstat": "Windows speech recognition", "$vcmdver": "Windows speech recognition",
    },
    "event": {"AGENT": "Microsoft Agent", "VCMD": "Windows speech recognition"},
}

# Implemented with a Linux equivalent that cannot be byte-for-byte identical.
SUBSTITUTE = {
    "command": {
        "/run": "xdg-open / direct exec; Windows programs via Wine when configured",
        "/speak": "speech-dispatcher instead of SAPI",
        "/registry": "no Windows registry; reads/writes a local INI-backed store",
    },
    "identifier": {
        "$os": "reports the Linux distribution; scripts testing for Windows versions see a non-Windows value",
        "$sysdir": "XDG directories in place of Windows system folders",
        "$shortfn": "no 8.3 names on Linux; returns the long name",
        "$longfn": "returns the path unchanged",
        "$speak": "speech-dispatcher instead of SAPI",
        "$registry": "no Windows registry; local INI-backed store",
        "$inmidi": "MIDI needs an optional synthesizer (Phase 6)",
    },
}

# Documented items added after the 7.72 help file, from the public changelog
# (https://www.mirc.com/versions.txt). Switch/property additions live in
# features.tsv so the whole 7.73-7.85 delta stays visible.
CHANGELOG_ADDITIONS = [
    ("command", "/copyini", "7.84", "copy INI topics"),
    ("command", "/markasread", "7.77", "mark a window as read"),
    ("command", "/drawdll", "7.83", "calls a custom DLL with a picture window"),
    ("identifier", "$darkmode", "7.79", "dark mode state"),
]
CHANGELOG_FEATURES = [
    ("/ban -a and $ibl().prefix/.mask/.account (IRCv3 EXTBAN/ACCOUNTEXTBAN)", "7.85"),
    ("/server -ircv3 [type]", "7.85"),
    ("/localinfo -w [URL]", "7.84"),
    ("$encode/$decode 'y' switch (Punycode)", "7.83"),
    ("$tip() delay -1 (permanent tip)", "7.82"),
    ("/server -lname <name> and -lerror [0|1]", "7.82"),
    ("Server option: disconnect on SASL login error", "7.82"),
    ("Dark mode support and $darkmode", "7.79"),
    ("/window -qS:P and $window().pbstate/.pbpercent", "7.78"),
    ("/write and $read() -WR switches", "7.78"),
    ("/findtext -hc switches", "7.78"),
    ("$encode/$decode 'x' switch (percent-encoding)", "7.77"),
    ("Scripts Editor find/replace: Count option, escape, remembered settings", "7.77"),
    ("$window().fulltitle", "7.77"),
    ("/dns -mn [name server]", "7.77"),
    ("IRCX mask \\ character email/userid support", "7.77"),
    ("$urlget() 'e' switch", "7.76"),
    ("Per-server option to connect/minimize on startup", "7.74"),
    ("IRCv3 extended-monitor tag in $notify()", "7.74"),
    ("/drawsave -v[pgj] and -du switches", "7.73"),
    ("$encode/$decode 'v' switch (Z85)", "7.73"),
    ("/drawpic -v (picture from &binvar)", "7.73"),
    ("$input() 'x' switch (dialog on desktop)", "7.73"),
    ("/copy -p (preserve timestamps)", "7.73"),
    ("$dialog().mapw/.maph", "7.73"),
]

# Events documented inside the "on X/Y" help pages but absent from the index.
EVENT_SUPPLEMENT = {
    "SERVERDEOP": "on_opodeop.html", "SERVERVOICE": "on_opodeop.html", "SERVERDEVOICE": "on_opodeop.html",
    "OWNER": "on_opodeop.html", "DEOWNER": "on_opodeop.html", "SERVERHELP": "on_opodeop.html", "SERVERDEHELP": "on_opodeop.html",
    "MIDIEND": "on_midiend.html", "WAVEEND": "on_midiend.html", "MP3END": "on_midiend.html",
    "SOCKWRITE": "sockets.html", "UDPREAD": "sockets.html",
    "CHATOPEN": "on_chatoserv.html", "CHATCLOSE": "on_chatoserv.html", "SERVOPEN": "on_chatoserv.html", "SERVCLOSE": "on_chatoserv.html",
}


def parse_hhk(path):
    src = open(path, encoding="utf-8", errors="replace").read()
    entries = []
    for block in re.findall(r'<OBJECT type="text/sitemap">(.*?)</OBJECT>', src, re.S | re.I):
        names = [html.unescape(v) for v in re.findall(r'name="Name"\s+value="([^"]*)"', block, re.I)]
        locals_ = [html.unescape(v) for v in re.findall(r'name="Local"\s+value="([^"]*)"', block, re.I)]
        if names:
            entries.append((names[0].replace("\xa0", " "), locals_[0].split("#")[0] if locals_ else ""))
    return entries


def classify(key):
    if key in SYNTAX_KEYS:
        return "syntax", key
    if key.startswith("/"):
        return "command", key
    if key.startswith("$"):
        return "identifier", key
    m = re.match(r"^on ([A-Z]+)$", key)
    if m:
        return "event", m.group(1)
    if key in ("CTCP prefix", "RAW prefix"):
        return "event", key.split()[0]
    return "feature", key


def load_existing(path):
    if not os.path.exists(path):
        return {}
    with open(path, newline="", encoding="utf-8") as f:
        return {row["name"]: row for row in csv.DictReader(f, delimiter="\t")}


def write_tsv(path, columns, rows):
    rows = sorted(rows, key=lambda r: (r.get("group", ""), r["name"].lower()))
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=columns, delimiter="\t", lineterminator="\n", extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hhk")
    ap.add_argument("--corpus", nargs="*", default=[], help="usage JSON files from scan_corpus.py")
    args = ap.parse_args()

    usage = collections.Counter()
    for p in args.corpus:
        d = json.load(open(p))
        for k in ("cmds", "idents"):
            for name, n in d.get(k, {}).items():
                usage[name.lower()] += n
        for name, n in d.get("events", {}).items():
            usage["event:" + name.upper()] += n
        usage["event:RAW"] += sum(d.get("raws", {}).values())
        usage["event:CTCP"] += sum(d.get("ctcps", {}).values())

    by_cat = collections.defaultdict(dict)
    for key, page in parse_hhk(args.hhk):
        cat, name = classify(key)
        by_cat[cat].setdefault(name, page)
    for name, page in EVENT_SUPPLEMENT.items():
        by_cat["event"].setdefault(name, page)
    for cat, name, since, note in CHANGELOG_ADDITIONS:
        by_cat[cat].setdefault(name, "versions.txt")

    os.makedirs(DATA, exist_ok=True)
    since_of = {(c, n): s for c, n, s, _ in CHANGELOG_ADDITIONS}
    note_of = {(c, n): t for c, n, _, t in CHANGELOG_ADDITIONS}
    counts = {}
    for cat in ("command", "identifier", "event", "syntax"):
        path = os.path.join(DATA, cat + "s.tsv" if cat != "syntax" else "syntax.tsv")
        existing = load_existing(path)
        rows = []
        for name, page in by_cat[cat].items():
            old = existing.get(name, {})
            status = old.get("status") or "planned"
            notes = old.get("notes") or note_of.get((cat, name), "")
            if not old:
                if name in NOT_POSSIBLE.get(cat, {}):
                    status, notes = "not-possible", NOT_POSSIBLE[cat][name]
                elif name in SUBSTITUTE.get(cat, {}):
                    status, notes = "planned", "substitute: " + SUBSTITUTE[cat][name]
            uses = usage.get(("event:" + name) if cat == "event" else name.lower(), 0)
            rows.append({
                "name": name, "status": status, "since": old.get("since") or since_of.get((cat, name), ""),
                "help_page": page, "corpus_uses": str(uses), "test": old.get("test", ""), "notes": notes,
            })
        write_tsv(path, ["name", "status", "since", "help_page", "corpus_uses", "test", "notes"], rows)
        counts[cat] = len(rows)

    # Feature topics, grouped by the help page they live on.
    path = os.path.join(DATA, "features.tsv")
    existing = load_existing(path)
    rows = []
    seen = set()
    for name, page in by_cat["feature"].items():
        group = page.replace(".html", "") or "misc"
        if (group, name) in seen:
            continue
        seen.add((group, name))
        old = existing.get(name, {})
        rows.append({"name": name, "group": group, "status": old.get("status") or "planned", "since": old.get("since", ""),
                     "test": old.get("test", ""), "notes": old.get("notes", "")})
    for name, since in CHANGELOG_FEATURES:
        old = existing.get(name, {})
        rows.append({"name": name, "group": "changelog-7.73-7.85", "status": old.get("status") or "planned",
                     "since": since, "test": old.get("test", ""), "notes": old.get("notes", "")})
    write_tsv(path, ["name", "group", "status", "since", "test", "notes"], rows)
    counts["feature"] = len(rows)
    print("imported:", ", ".join(f"{k}={v}" for k, v in counts.items()))


if __name__ == "__main__":
    sys.exit(main())
