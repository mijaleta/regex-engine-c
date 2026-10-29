#!/bin/bash
# Verifier for the rx task.
#
# 1. Stops any process still running as the agent user and makes this
#    directory root-only, so neither the build nor the candidate can read it.
# 2. Copies /app (without .git/, build outputs removed) to a fresh directory
#    and runs `make rx` there as the agent user.
# 3. Runs every case in cases/ against a root-owned copy of the binary, as the
#    agent user. A case passes when stdout matches byte for byte and the exit
#    status matches; stderr is ignored. A case that expects an error also has
#    a control command line that must succeed, so a program that rejects
#    everything gets no credit for rejecting bad input.
# 4. Writes passed / total to /logs/verifier/reward.txt.
set -u
TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGS=/logs/verifier
ROOT=/tmp/rx-verify
CASE_TIMEOUT=10
mkdir -p "$LOGS"
echo 0 > "$LOGS/reward.txt"

pkill -9 -u agent 2>/dev/null
chmod 700 "$TESTS_DIR"
rm -rf "$ROOT"
mkdir -p "$ROOT/bin" "$ROOT/home"
chmod 755 "$ROOT" "$ROOT/bin"

if [ ! -f /app/Makefile ]; then
    echo "no Makefile in /app"
    exit 0
fi
cp -a /app "$ROOT/src"
rm -rf "$ROOT/src/.git" "$ROOT/src/rx"
find "$ROOT/src" -name '*.o' -delete
chown -R agent:agent "$ROOT/src" "$ROOT/home"

AS_AGENT=(runuser -u agent -- env -i PATH=/usr/bin:/bin HOME="$ROOT/home" LANG=C.UTF-8)
if ! (cd "$ROOT/src" && timeout -s KILL 900 "${AS_AGENT[@]}" make rx) > "$LOGS/build.log" 2>&1 \
    || [ ! -f "$ROOT/src/rx" ]; then
    echo "build failed:"
    tail -50 "$LOGS/build.log"
    exit 0
fi
# Run a root-owned copy so a candidate cannot rewrite its own binary mid-suite.
install -m 755 "$ROOT/src/rx" "$ROOT/bin/rx"
cd "$ROOT/home"

run_one() {
    local d="$1" args=() code
    mapfile -d '' -t args < "$d/args"
    timeout -s KILL "$CASE_TIMEOUT" "${AS_AGENT[@]}" "$ROOT/bin/rx" "${args[@]}" \
        < "$d/stdin" > "$ROOT/out" 2>/dev/null
    code=$?
    [ "$code" = "$(cat "$d/code")" ] && cmp -s "$ROOT/out" "$d/stdout"
}

declare -A cat_pass cat_total
passed=0
total=0
: > "$LOGS/failed.txt"
for d in "$TESTS_DIR"/cases/*/; do
    d="${d%/}"
    cat="$(cat "$d/category")"
    total=$((total + 1))
    cat_total[$cat]=$(( ${cat_total[$cat]:-0} + 1 ))
    if run_one "$d" && { [ ! -d "$d/control" ] || run_one "$d/control"; }; then
        passed=$((passed + 1))
        cat_pass[$cat]=$(( ${cat_pass[$cat]:-0} + 1 ))
    else
        basename "$d" >> "$LOGS/failed.txt"
    fi
done
pkill -9 -u agent 2>/dev/null

for cat in $(printf '%s\n' "${!cat_total[@]}" | sort); do
    printf '%-12s %3d/%d\n' "$cat" "${cat_pass[$cat]:-0}" "${cat_total[$cat]}"
done
printf 'total        %3d/%d\n' "$passed" "$total"
reward="$(awk -v p="$passed" -v t="$total" 'BEGIN { printf "%.4f", (t ? p / t : 0) }')"
echo "$reward" > "$LOGS/reward.txt"
echo "reward: $reward"
