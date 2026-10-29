#!/bin/bash
# Builds rx with make and runs the public example cases.
#
#   /app/run_examples.sh        # summary
#   /app/run_examples.sh -v     # also show every failing case
#
# Each case directory in examples/ holds args (each argument followed by a NUL
# byte), stdin, the expected stdout and the expected exit code. A case that
# expects an error also has a control/ directory with a valid command line
# that must succeed too.
set -u
APP="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

if ! make -C "$APP" -s rx; then
    echo "build failed"
    exit 1
fi
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

run_one() {
    local d="$1" args=() code want
    mapfile -d '' -t args < "$d/args"
    timeout -s KILL 10 "$APP/rx" "${args[@]}" < "$d/stdin" > "$OUT" 2>/dev/null
    code=$?
    want="$(cat "$d/code")"
    if [ "$code" = "$want" ] && cmp -s "$OUT" "$d/stdout"; then
        return 0
    fi
    if [ "$VERBOSE" = 1 ]; then
        printf -- '--- %s: rx' "$d"
        printf ' %q' "${args[@]}"
        printf '\nexpected exit %s, got %s\n' "$want" "$code"
        diff <(cat "$d/stdout") "$OUT" | head -20
    fi
    return 1
}

passed=0
total=0
for d in "$APP"/examples/*/; do
    d="${d%/}"
    total=$((total + 1))
    if run_one "$d" && { [ ! -d "$d/control" ] || run_one "$d/control"; }; then
        passed=$((passed + 1))
    fi
done
echo "$passed/$total example cases pass"
