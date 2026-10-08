#!/bin/sh
# Stage 3 measurements. For each variant of target/solve.c:
#   1. host-test the same code (bench/check_target.c, sample of states),
#   2. build it with gcc -O2 for RV32I,
#   3. run it on Ripes RV32_ISS and report retired instructions,
#      .text bytes and static data bytes (.rodata + .data + .bss).
#
# Usage, from an MSYS2 terminal in the repository root:
#   sh target/measure.sh [STATE ...]
# Default states: the worst state of the chosen design and the vector the
# assignment asks to report.
set -e
cd "$(dirname "$0")/.."
# The RISC-V toolchain needs the UCRT64 runtime DLLs; the host tools are
# linked statically so that they need none.
export PATH="/ucrt64/bin:$PATH"

HOSTCC=${HOSTCC:-/mingw64/bin/gcc}
RVCC=${RVCC:-/ucrt64/bin/riscv64-unknown-elf-gcc}
RVSIZE=${RVSIZE:-/ucrt64/bin/riscv64-unknown-elf-size}
RIPES=${RIPES:-/c/tools/Ripes/Ripes.exe}
STATES=${*:-"54721631111111 21345671111111"}
OUT=build/stage3
HOSTFLAGS="-O2 -std=c99 -Wall -Wextra -Wno-sign-compare -static"
RVFLAGS="-O2 -march=rv32i -mabi=ilp32 -nostdlib -ffreestanding -Wall -Wextra"

mkdir -p "$OUT"
$HOSTCC $HOSTFLAGS bench/gen_tables.c -o "$OUT/gen_tables"
"$OUT/gen_tables" | tr -d '\r' > target/tables.h

printf '%-6s %-15s %12s %7s %8s %4s  %s\n' \
    variant state iret text static exit solution
while IFS=: read -r name flags; do
    [ -n "$name" ] || continue
    $HOSTCC $HOSTFLAGS $flags bench/check_target.c -o "$OUT/check_$name"
    if ! "$OUT/check_$name" sample > "$OUT/check_$name.log"; then
        echo "$name: host test FAILED, see $OUT/check_$name.log"
        exit 1
    fi
    for state in $STATES; do
        elf="$OUT/$name-$state.elf"
        $RVCC $RVFLAGS $flags -DSTATE="\"$state\"" target/solve.c -o "$elf"
        text=$($RVSIZE -A "$elf" | awk '$1 == ".text" { print $2 }')
        static=$($RVSIZE -A "$elf" | awk '$1 ~ /^\.(s?rodata|s?data|s?bss)/ \
            { sum += $2 } END { print sum + 0 }')
        run=$("$RIPES" --mode cli --src "$elf" -t elf --proc RV32_ISS --iret \
            2>&1 | tr -d '\r\000')
        iret=$(printf '%s\n' "$run" | awk '/instructions retired/ { getline; print }')
        code=$(printf '%s\n' "$run" | sed -n 's/.*exited with code: //p')
        moves=$(printf '%s\n' "$run" | head -n 1)
        printf '%-6s %-15s %12s %7s %8s %4s  %s\n' \
            "$name" "$state" "$iret" "$text" "$static" "$code" "$moves"
    done
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
