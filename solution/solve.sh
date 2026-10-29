#!/bin/bash
# Reference solution: installs the complete rx implementation into /app/src.
set -euo pipefail
SOLUTION_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

rm -f /app/src/*.c /app/src/*.h
cp "$SOLUTION_DIR"/rx/*.c "$SOLUTION_DIR"/rx/*.h /app/src/

cd /app
make rx
./run_examples.sh
