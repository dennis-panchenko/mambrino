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
led    0  .#...#...#...
stopped at HALT (address 7) after 13 ticks"

out="$("$BIN" run --set A=1 "$FIX/count3.asm" | tail -1)"
check "--set is overwritten by the program's SET" "$out" "stopped at HALT (address 7) after 13 ticks"

out="$("$BIN" run -m LOOP --cycles 7 "$FIX/no_machine.asm" | tail -2)"
check "-m supplies the machine; --cycles limits the run" "$out" "led    0  #..#..#
still running after 7 ticks (the cycle limit)"

out="$("$BIN" run "$FIX/empty.asm")"
check "an empty program runs no ticks" "$out" "LAMP, 0 bytes
led       (no ticks)
stopped at HALT (address 0) after 0 ticks"

out="$("$BIN" run "$FIX/bad_byte.asm" | tail -1)"
check "a bad byte is reported with its address" "$out" "hit a byte that is not an instruction (address 1) after 1 tick"

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

out="$("$BIN" verify lamp/blink "$FIX/blink_long.asm")"
check "verify shows want and got" "$out" "FAIL  One blink: tick 1: the light should be off, but it is on.
      want  #..
      got   ##.
             ^"

out="$(cd tests/fixtures && "../../$BIN" verify lamp/blink ../solutions/lamp/blink.asm)"
check "challenge ids work from any directory" "$out" "PASS  One blink: 3 bytes, 2 ticks"

out="$(cd /tmp && MAMBRINO_CHALLENGES="$OLDPWD/challenges/lamp" "$OLDPWD/$BIN" show light-on | head -1)"
check "MAMBRINO_CHALLENGES overrides the location" "$out" "Light on (LAMP)"

out="$("$BIN" show challenges/lamp/light-on.txt)"
check "show by path" "$out" "Light on (LAMP)

Turn the light on and stop.

Target:  on until the program stops"

out="$("$BIN" show loop/warm-up | tail -1)"
check "show draws a lead-in and a loop" "$out" "Target:  ####  then  ..#  over and over, forever"

out="$("$BIN" show count/blink-a | tail -3)"
check "show gives one target per case" "$out" "Target, with A = 1:  on and off 1 time, any timing, ending off
Target, with A = 3:  on and off 3 times, any timing, ending off
Target, with A = 7:  on and off 7 times, any timing, ending off"

out="$("$BIN" machines | grep -c '^[A-Z]*: ')"
check "machines lists every machine" "$out" "3"

"$BIN" frob >/dev/null 2>&1
check "unknown command (exit 2)" "$?" "2"

exit $fail
