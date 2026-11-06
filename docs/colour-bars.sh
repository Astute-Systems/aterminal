#!/bin/sh
# Prints a colour reference chart. Run it inside aterminal with --colour=colour:
#   aterminal --colour=colour --geometry=80x25 --hold -- sh docs/colour-bars.sh

reset=$(printf '\033[0m')

# Prints spaces in the given background colour.
block() { printf '\033[%sm%*s%s' "$1" "$2" '' "$reset"; }

printf '\033[?25l\033[2J\033[H\033[1m  ATERMINAL COLOUR REFERENCE%s\n\n' "$reset"

# Standard bars at 75 percent, as on a broadcast test card.
for row in 1 2 3 4 5; do
    printf '  '
    for colour in '191;191;191' '191;191;0' '0;191;191' '0;191;0' '191;0;191' '191;0;0' '0;0;191'; do
        block "48;2;$colour" 11
    done
    printf '\n'
done
printf '  '
for colour in '0;0;191' '19;19;19' '191;0;191' '19;19;19' '0;191;191' '19;19;19' '191;191;191'; do
    block "48;2;$colour" 11
done
printf '\n\n'

# Each row below is 64 cells wide after its 15-column label.
printf '  ANSI 0-7     '
for colour in 40 41 42 43 44 45 46 47; do block "$colour" 8; done
printf '\n  ANSI 8-15    '
for colour in 100 101 102 103 104 105 106 107; do block "$colour" 8; done
printf '\n\n  256 cube     '
cell=0
while [ "$cell" -lt 64 ]; do
    block "48;5;$((16 + cell * 216 / 64))" 1
    cell=$((cell + 1))
done
printf '\n  256 grey     '
cell=0
while [ "$cell" -lt 64 ]; do
    block "48;5;$((232 + cell * 24 / 64))" 1
    cell=$((cell + 1))
done
printf '\n\n  True red     '
for step in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do block "48;2;$((step * 17));0;0" 4; done
printf '\n  True green   '
for step in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do block "48;2;0;$((step * 17));0" 4; done
printf '\n  True blue    '
for step in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do block "48;2;0;0;$((step * 17))" 4; done
printf '\n\n  Text  \033[31mred\033[32m green\033[33m yellow\033[34m blue\033[35m magenta\033[36m cyan\033[37m white%s  \033[1;31mbright\033[32m bold\033[34m colour%s\n' "$reset" "$reset"
