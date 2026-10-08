#!/bin/sh
# Stage 3 operation counts: runs the search of every variant on the host,
# built from the same target/ida.h with -DIDA_STATS, and prints how many
# iterations, expansions, children, pushes, PDB loads and next-bound
# comparisons it performs. Run target/measure.sh first, which generates
# target/tables.h.
#
# Usage, from an MSYS2 terminal in the repository root:
#   sh target/count.sh [STATE ...]
set -e
cd "$(dirname "$0")/.."
# host tools are linked statically, so they need no runtime DLLs
export PATH="/ucrt64/bin:$PATH"

HOSTCC=${HOSTCC:-/mingw64/bin/gcc}
STATES=${*:-"54721631111111 21345671111111"}
OUT=build/stage3
HOSTFLAGS="-O2 -std=c99 -Wall -Wextra -Wno-sign-compare -static -DIDA_STATS"

mkdir -p "$OUT"
while IFS=: read -r name flags; do
    [ -n "$name" ] || continue
    $HOSTCC $HOSTFLAGS $flags bench/check_target.c -o "$OUT/count_$name"
    echo "== $name"
    "$OUT/count_$name" count $STATES
done <<'EOF'
base:
O2:-DO2=1
O3:-DO3=1
O4:-DO4=1
O5:-DO5=1
O6:-DO6=1
O7:-DO7=1
all:-DO2=1 -DO3=1 -DO4=1 -DO5=1 -DO6=1 -DO7=1
EOF
