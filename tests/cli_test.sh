#!/usr/bin/env bash
# Exercises the real bin/mambrino end to end: argument parsing, file
# loading, output format and exit codes.
set -u
BIN="${1:?usage: cli_test.sh <path-to-mambrino-binary>}"
cd "$(dirname "$0")/.."
FIX=tests/fixtures
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

out="$("$BIN" run "$FIX/count3.asm")"
check "run prints the timeline" "$out" "COUNT, 8 bytes
led
     0  .#...#...#...
stopped at HALT after 13 ticks, at address 8"

out="$("$BIN" run --set A=1 "$FIX/count3.asm" | tail -1)"
check "--set is overwritten by the program's SET" "$out" "stopped at HALT after 13 ticks, at address 8"

out="$("$BIN" run -m LOOP --cycles 7 "$FIX/no_machine.asm" | tail -2)"
check "-m supplies the machine; --cycles limits the run" "$out" "     0  #..#..#
still running after 7 ticks (the cycle limit)"

"$BIN" run "$FIX/no_machine.asm" >/dev/null 2>&1
check "no machine is an error (exit 2)" "$?" "2"

out="$("$BIN" run "$FIX/bad.asm" 2>&1)"
check "assembler errors give file:line:column" "$out" "$FIX/bad.asm:3:3: JMP is not an instruction on LAMP"

"$BIN" verify lamp/blink "$FIX/count3.asm" >/dev/null
check "wrong machine fails verify (exit 1)" "$?" "1"

out="$("$BIN" verify loop/forever "$FIX/no_machine.asm")"
check "verify by id, machine from the challenge" "$out" "PASS  Forever: 4 bytes, 60 ticks"

"$BIN" verify no/such "$FIX/count3.asm" >/dev/null 2>&1
check "missing challenge (exit 2)" "$?" "2"

out="$("$BIN" show challenges/lamp/light-on.txt)"
check "show by path" "$out" "Light on (LAMP)

Turn the light on and stop."

out="$("$BIN" machines | grep -c '^[A-Z]*: ')"
check "machines lists every machine" "$out" "3"

"$BIN" frob >/dev/null 2>&1
check "unknown command (exit 2)" "$?" "2"

exit $fail
