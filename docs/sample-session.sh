#!/bin/sh
# Prints the scripted sample session used for the README screenshots.
# Example: aterminal --green --text-size=2 --size=960x600 -- sh docs/sample-session.sh

printf '\033[?25l\033[2J\033[H'
p() { printf '\033[1;32muser@aterminal\033[0m:\033[1;34m~/repos/aterminal\033[0m$ %s\n' "$1"; }
p 'ls --color -F src tests'
printf '\033[1;34msrc/\033[0m\n  display.cpp  \033[1;32mmain.cpp\033[0m  motd.cpp  prompt.cpp  session.cpp  terminal.cpp\n\033[1;34mtests/\033[0m\n  display_test.cpp  motd_test.cpp  parser_test.cpp  pty_test.cpp\n\n'
p 'git log --oneline --decorate -3'
printf '\033[33m0fae383\033[0m (\033[1;36mHEAD -> \033[1;32mmain\033[0m) Merge feature work\n\033[33m766ab0c\033[0m Add icon, man page and installer\n\033[33m950858c\033[0m (\033[1;31morigin/main\033[0m) Updated\n\n'
p 'git diff --stat'
printf ' src/display.cpp | 40 \033[32m++++++++++++\033[31m--\033[0m\n src/session.cpp | 21 \033[32m+++++++\033[31m---\033[0m\n\n'
p 'ctest'
printf '\033[32m100%% tests passed\033[0m, 0 tests failed out of 34\n\n\033[30;42m PASS \033[0m parser  \033[30;42m PASS \033[0m pty  \033[30;43m SLOW \033[0m sdl  \033[97;41m FAIL \033[0m none\n\n'
printf '\033[1;32muser@aterminal\033[0m:\033[1;34m~/repos/aterminal\033[0m$ '
