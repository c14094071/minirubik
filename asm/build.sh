#!/bin/sh
# Build, measure and test the hand-written RV32I solver, asm/main.S.
#
# Usage, from an MSYS2 terminal in the repository root:
#   sh asm/build.sh run STATE   build for STATE, run it on Ripes RV32_ISS and
#                               report retired instructions, .text and
#                               static data
#   sh asm/build.sh test        run every state in tests/solutions.txt and
#                               asm/tests.txt; each must exit 0 with a
#                               solution of exactly its BFS distance
#   sh asm/build.sh gui STATE   write build/asm/gui.s (renderer included)
#                               for loading in the Ripes GUI
#
# main.S goes through the C preprocessor with -DRENDER=0 or 1 and
# -DINPUT='"STATE"' (Ripes has no .if), and the generated asm/tables.s is
# appended to the result (Ripes has no .include). The CLI build is linked
# with GNU ld, so that .text is the size of linked code; --no-relax keeps
# every `la` at two instructions, because Ripes does not set gp. The same
# CLI source is also assembled by Ripes itself, and both runs must retire
# the same number of instructions.
set -e
cd "$(dirname "$0")/.."
export PATH="/ucrt64/bin:$PATH"

HOSTCC=${HOSTCC:-/mingw64/bin/gcc}
RIPES=${RIPES:-/c/tools/Ripes/Ripes.exe}
OUT=build/asm
HOSTFLAGS="-O2 -std=c99 -Wall -Wextra -Wno-sign-compare -static"

[ -f asm/main.S ] || { echo "asm/main.S not found"; exit 1; }
mkdir -p "$OUT"
$HOSTCC $HOSTFLAGS bench/gen_tables.c -o "$OUT/gen_tables"
"$OUT/gen_tables" asm | tr -d '\r' > asm/tables.s

# preprocess RENDER STATE OUTPUT
preprocess() {
    riscv64-unknown-elf-gcc -E -P -x assembler-with-cpp -DRENDER="$1" \
        -DINPUT="\"$2\"" asm/main.S > "$3.tmp"
    cat "$3.tmp" asm/tables.s > "$3"
    rm -f "$3.tmp"
}

# build_and_run STATE: sets elf_out, asm_out, iret, code, moves
build_and_run() {
    preprocess 0 "$1" "$OUT/cli.s"
    riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -nostdlib -Wl,--no-relax \
        -o "$OUT/cli.elf" "$OUT/cli.s"
    elf_out=$("$RIPES" --mode cli --src "$OUT/cli.elf" -t elf --proc RV32_ISS \
        --iret 2>&1 | tr -d '\r\000')
    asm_out=$("$RIPES" --mode cli --src "$OUT/cli.s" -t asm --proc RV32_ISS \
        --iret 2>&1 | tr -d '\r\000')
    iret=$(printf '%s\n' "$elf_out" | awk '/instructions retired/ { getline; print }')
    asm_iret=$(printf '%s\n' "$asm_out" | awk '/instructions retired/ { getline; print }')
    code=$(printf '%s\n' "$elf_out" | sed -n 's/.*exited with code: //p')
    moves=$(printf '%s\n' "$elf_out" | head -n 1)
    if [ "$iret" != "$asm_iret" ]; then
        echo "Ripes assembler and GNU ld disagree: iret $asm_iret vs $iret"
        printf '%s\n' "--- Ripes assembler output:" "$asm_out"
        exit 1
    fi
}

case "$1" in
run)
    [ -n "$2" ] || { echo "usage: sh asm/build.sh run STATE"; exit 2; }
    build_and_run "$2"
    printf '%s\n' "$elf_out"
    text=$(riscv64-unknown-elf-size -A "$OUT/cli.elf" | awk '$1 == ".text" { print $2 }')
    static=$(riscv64-unknown-elf-size -A "$OUT/cli.elf" |
        awk '$1 ~ /^\.(s?rodata|s?data|s?bss)/ { sum += $2 } END { print sum + 0 }')
    echo "===== .text bytes"
    echo "$text"
    echo "===== static data bytes (.data + .rodata + .bss)"
    echo "$static"
    ;;
test)
    $HOSTCC $HOSTFLAGS bench/compare.c -o "$OUT/compare"
    states=$(sed -n 's/^\([1-7]\{7\}[1-3]\{7\}\).*/\1/p' tests/solutions.txt \
        $( [ -f asm/tests.txt ] && echo asm/tests.txt ))
    pass=0
    fail=0
    for state in $states; do
        d=$("$OUT/compare" search D --prune "$state" | sed -n 's/.* d=\([0-9]*\) .*/\1/p')
        build_and_run "$state"
        n=$(printf '%s\n' "$moves" | wc -w)
        if [ "$code" = 0 ] && [ "$n" = "$d" ]; then
            result=PASS
            pass=$((pass + 1))
        else
            result=FAIL
            fail=$((fail + 1))
        fi
        printf '%s %s  d=%-2s moves=%-2s exit=%-2s iret=%-10s %s\n' \
            "$result" "$state" "$d" "$n" "$code" "$iret" "$moves"
    done
    echo "$pass passed, $fail failed"
    [ "$fail" = 0 ]
    ;;
gui)
    [ -n "$2" ] || { echo "usage: sh asm/build.sh gui STATE"; exit 2; }
    preprocess 1 "$2" "$OUT/gui.s"
    echo "wrote $OUT/gui.s; open it in the Ripes GUI editor"
    ;;
*)
    sed -n '2,13p' "$0"
    exit 2
    ;;
esac
