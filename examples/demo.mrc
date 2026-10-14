; demo.mrc — a small remote script for lurker-desktop (MSL)
; Greets people who say hello, and adds a !roll dice command.

on *:TEXT:*hello*:#: {
  echo [bot] $nick greeted the channel on $chan
  msg $chan hi $nick $+ !
}

on *:TEXT:!roll*:#: {
  msg $chan $nick rolls a die: $rand(1,6)
}

on *:JOIN:#: {
  echo [bot] $nick joined $chan
}

alias slap {
  describe $active slaps $1 around a bit with a large trout
}
