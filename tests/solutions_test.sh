#!/usr/bin/env bash
# Every challenge must have a reference solution in tests/solutions/<id>.asm
# that passes, so no puzzle ships unsolvable.
set -u
BIN="${1:?usage: solutions_test.sh <path-to-mambrino-binary>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
count=0
cd "$ROOT"
for f in challenges/*/*.txt; do
    id="${f#challenges/}"
    id="${id%.txt}"
    sol="tests/solutions/$id.asm"
    count=$((count + 1))
    if [ ! -f "$sol" ]; then
        echo "FAIL: $id has no reference solution ($sol)"
        fail=1
    elif out="$("$BIN" verify "$id" "$sol")"; then
        echo "$out"
    else
        echo "FAIL: $id: $out"
        fail=1
    fi
done
echo "$count challenges checked"
exit $fail
