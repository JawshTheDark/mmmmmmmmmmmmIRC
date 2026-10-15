#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
# run-headless.sh — run a cases file through the owner's real mIRC 7.85 under
# Wine + Xvfb, with no desktop and no manual clicking, and write the observed
# results to an output file (one "<lineNo><TAB><result>" per case; a case that
# errors in mIRC writes no line). This is the oracle: only observed behaviour is
# recorded, nothing is taken from the binary. See README.md.
#
# Usage:
#   tools/oracle/run-headless.sh <cases-file> <results-file> [mirc.exe]
#
# Needs (Ubuntu): wine (+ 32-bit: wine32:i386), xvfb, xdotool, imagemagick,
# matchbox-window-manager. mirc.exe defaults to ~/mirc.exe.
#
# How it works: mIRC is a 32-bit GUI app that shows, on a fresh prefix, an
# evaluation "About mIRC" nag, then a Connect/Options dialog, and warns before
# running a typed /command. A pure win32 WINEPREFIX runs the 32-bit binary
# reliably (the wow64 path on Ubuntu's wine 9.0 fails to load syswow64 ntdll).
# A tiny window manager (matchbox) gives dependable focus; we dismiss the two
# dialogs by clicking their buttons at fixed coords on a 1024x768 root, disable
# the command-safety warning once, then type `/load` + `/probe` and wait for the
# done.txt sentinel probe.mrc drops beside the results.
set -euo pipefail

if [[ "${1:-}" == "--inner" ]]; then
  # ---- inner stage: runs under xvfb-run, DISPLAY is set ------------------
  set +e   # best-effort GUI poking; success is measured by the done.txt sentinel
  ODIR="$2"
  export WINEPREFIX WINEDLLOVERRIDES="mscoree=;mshtml=" WINEDEBUG=-all
  matchbox-window-manager -use_titlebar no -use_cursor no >/tmp/oracle-wm.log 2>&1 &
  sleep 1
  ( cd "$WINEPREFIX/drive_c/mirc" && wine mirc.exe >/tmp/oracle-mirc.log 2>&1 ) &

  # Close the two startup dialogs (About eval nag → OK; Connect/Options → OK).
  for _ in $(seq 1 25); do
    if xdotool search --name "About mIRC" >/dev/null 2>&1; then
      xdotool mousemove 566 498 click 1; sleep 1; continue   # About: OK
    fi
    if xdotool search --name "mIRC Options" >/dev/null 2>&1; then
      xdotool mousemove 252 471 click 1; sleep 1; continue   # Options: OK
    fi
    sleep 1
    xdotool search --name "About mIRC"  >/dev/null 2>&1 && continue
    xdotool search --name "mIRC Options" >/dev/null 2>&1 && continue
    break
  done

  handle_cmd_warning() {  # uncheck "Always show this message", then Yes
    for _ in 1 2 3 4; do
      if xdotool search --name "mIRC Command Warning" >/dev/null 2>&1; then
        xdotool mousemove 337 497 click 1; sleep 1
        xdotool mousemove 564 470 click 1; sleep 1
        return 0
      fi
      sleep 1
    done
  }
  run_cmd() {  # type a command into the status editbox and submit it
    xdotool mousemove 400 473 click 1; sleep 1
    xdotool type --delay 40 "$1"
    sleep 1; xdotool key --clearmodifiers Return; sleep 1
    handle_cmd_warning
  }

  MID=$(xdotool search --name "^mIRC" 2>/dev/null | tail -1)
  xdotool windowactivate --sync "$MID" 2>/dev/null; sleep 1
  run_cmd '/load -rs C:\mirc\probe.mrc'
  run_cmd '/probe C:\oracle\cases.txt C:\oracle\results.txt'

  for _ in $(seq 1 40); do [[ -f "$ODIR/done.txt" ]] && break; sleep 1; done
  sleep 1
  exit 0
fi

# ---- outer stage: set up the prefix, stage files, run, collect ----------
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASES="${1:?usage: run-headless.sh <cases-file> <results-file> [mirc.exe]}"
RESULTS="${2:?usage: run-headless.sh <cases-file> <results-file> [mirc.exe]}"
MIRC_EXE="${3:-$HOME/mirc.exe}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-mirc}"

for t in wine xvfb-run xdotool matchbox-window-manager import; do
  command -v "$t" >/dev/null || { echo "missing tool: $t" >&2; exit 3; }
done
[[ -f "$MIRC_EXE" ]] || { echo "mirc.exe not found: $MIRC_EXE" >&2; exit 3; }
[[ -f "$CASES" ]]    || { echo "cases file not found: $CASES" >&2; exit 3; }

# A pure 32-bit prefix is required (wow64 on Ubuntu wine 9.0 can't load the
# 32-bit ntdll). Create it once; reuse it afterwards.
if [[ ! -f "$WINEPREFIX/system.reg" ]] || ! grep -q '#arch=win32' "$WINEPREFIX/system.reg"; then
  echo "creating 32-bit Wine prefix at $WINEPREFIX ..." >&2
  rm -rf "$WINEPREFIX"
  WINEARCH=win32 WINEDLLOVERRIDES="mscoree=;mshtml=" WINEDEBUG=-all \
    xvfb-run -a -s "-screen 0 1024x768x24" wineboot -i >/dev/null 2>&1 || true
fi

P="$WINEPREFIX/drive_c"
mkdir -p "$P/mirc" "$P/oracle"
cp "$MIRC_EXE" "$P/mirc/mirc.exe"
: > "$P/mirc/mirc.ini"                       # empty ini next to mirc.exe = portable mode
cp "$HERE/probe.mrc" "$P/mirc/probe.mrc"
cp "$CASES" "$P/oracle/cases.txt"
rm -f "$P/oracle/results.txt" "$P/oracle/done.txt"

timeout 180 xvfb-run -a -s "-screen 0 1024x768x24" \
  bash "${BASH_SOURCE[0]}" --inner "$P/oracle" || true
wineserver -k 2>/dev/null || true
pkill -f matchbox-window-manager 2>/dev/null || true

if [[ -f "$P/oracle/results.txt" ]]; then
  tr -d '\r' < "$P/oracle/results.txt" > "$RESULTS"
  echo "wrote $(wc -l < "$RESULTS") results to $RESULTS" >&2
else
  echo "no results produced (see /tmp/oracle-mirc.log)" >&2
  exit 1
fi
