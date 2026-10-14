; This Source Code Form is subject to the terms of the Mozilla Public
; License, v. 2.0. If a copy of the MPL was not distributed with this
; file, You can obtain one at http://mozilla.org/MPL/2.0/.
;
; probe.mrc — differential-testing probe. This is OUR script; it is loaded into a
; real copy of mIRC (under Wine) to record what mIRC does with each test case,
; so the engine in this repository can be checked against observed behaviour.
;
; Usage inside mIRC:   /load -rs probe.mrc
;                      /probe C:\oracle\cases.txt C:\oracle\results.txt
;
; cases.txt: one case per line. Lines starting with ; are comments.
;   - An expression line (anything not starting with /) is evaluated and written
;     to results.txt as:  <line number> <TAB> <result>
;   - A command line (starting with /) is executed as typed; it is responsible
;     for writing its own result with:  write -n %probe.out <line number> $+ $chr(9) $+ <text>
;     The current line number is in %probe.line.
; A case that raises a script error writes nothing; a missing line number in
; results.txt therefore means "mIRC reported an error for this case".
; Each case runs from its own timer tick so an error in one case cannot stop the run.

alias probe {
  if ($isfile($1) == $false) { echo -s probe: cases file not found: $1 | return }
  if ($2 == $null) { echo -s probe: usage: /probe <cases file> <results file> | return }
  set %probe.cases $1
  set %probe.out $2
  set %probe.i 1
  if ($isfile(%probe.out)) { .remove %probe.out }
  echo -s probe: running $lines(%probe.cases) cases from %probe.cases
  .timerprobe 1 0 probe.next
}

alias probe.next {
  if (%probe.i > $lines(%probe.cases)) {
    echo -s probe: finished, results in %probe.out
    unset %probe.*
    return
  }
  set %probe.line %probe.i
  inc %probe.i
  ; arm the next case before evaluating this one, so an error here does not end the run
  .timerprobe 1 0 probe.next
  var %case = $read(%probe.cases, n, %probe.line)
  if (%case == $null) || ($left(%case, 1) == ;) { return }
  if ($left(%case, 1) == /) {
    %case
    return
  }
  write -n %probe.out %probe.line $+ $chr(9) $+ $eval(%case, 2)
}
