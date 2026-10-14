; demo_dialog.mrc — shows an MSL /dialog rendered as a real Qt dialog
dialog greet {
  title "Lurker Desktop - MSL Dialog"
  size -1 -1 170 120
  text "What is your name?", 1, 10 8 150 10
  edit "", 2, 10 22 150 12
  button "Greet", 3, 10 44 80 16, ok
  button "Close", 4, 95 44 65 16, cancel
  text "", 5, 10 70 150 40
}
on *:dialog:greet:init:0: {
  did -o greet 2 world
  did -o greet 5 Dialog built from an MSL table. Edit 2 = $qt($did(greet,2)) (set by on DIALOG init).
}
on *:dialog:greet:sclick:3: { did -o greet 5 Hello $did(greet,2) $+ ! }
alias greet { dialog -m greet greet }
