#!/usr/bin/env bash
# Drives bin/mambrino repl through stdin, as a player would type.
set -u
BIN="${1:?usage: repl_test.sh <path-to-mambrino-binary>}"
cd "$(dirname "$0")/.."
fail=0

check() {
    if [ "$2" != "$3" ]; then
        echo "FAIL: $1"
        echo "  got:  [$2]"
        echo "  want: [$3]"
        fail=1
    else
        echo "PASS: $1"
    fi
}

out="$(printf 'step\nstep 2\nstate\nquit\n' | "$BIN" repl tests/fixtures/count3.asm | sed 's/mambrino> //g' | sed 's/ *$//' | sed '/^$/d')"
check "load, step, state" "$out" "loaded tests/fixtures/count3.asm: COUNT, 8 bytes
pc 0    A=0    led off  tick 0     next: SET 3
pc 2    A=3    led off  tick 1     next: ON
pc 4    A=3    led off  tick 3     next: DEC
pc 4    A=3    led off  tick 3     next: DEC"

out="$(printf 'break 2\nrun\nrun\nclear 2\nrun\n' | "$BIN" repl tests/fixtures/count3.asm | sed 's/mambrino> //g' | sed 's/ *$//' | sed '/^$/d' | tail -5)"
check "breakpoints stop run" "$out" "break at 2
pc 2    A=3    led off  tick 1     next: ON
break at 2
pc 2    A=2    led off  tick 5     next: ON
pc 7    A=0    led off  tick 13    stopped at HALT"

out="$(printf 'list\n' | "$BIN" repl tests/fixtures/count3.asm | sed 's/mambrino> //g' | sed 's/ *$//' | sed '/^$/d' | tail -7)"
check "list shows code with line numbers" "$out" ">    0  20 03  SET 3       line 2
     2  01     ON          line 3
     3  02     OFF         line 4
     4  21     DEC         line 5
     5  11 02  JNZ 2       line 6
     7  00     HALT        line 7
         8-15   (empty: HALT)"

out="$(printf 'machine LOOP\nload tests/fixtures/no_machine.asm\nrun 5\n' | "$BIN" repl | sed 's/mambrino> //g' | sed 's/ *$//' | sed '/^$/d' | tail -2)"
check "machine command is the fallback for load" "$out" "paused after 5 ticks
pc 2    led off  tick 5     next: JMP 0"

out="$(printf 'step\nfrob\n' | "$BIN" repl | sed 's/mambrino> //g' | sed 's/ *$//' | sed '/^$/d')"
check "no machine yet; unknown command" "$out" "no machine yet: use 'machine NAME' or 'load FILE'
unknown command 'frob'; try 'help'"

exit $fail
