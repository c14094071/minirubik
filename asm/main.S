# Hand-written RV32I solver for the 2x2x2 cube (Stage 4).
#
# Conventions (see asm/build.sh):
#   INPUT is the 14-digit state, substituted by the C preprocessor.
#   Code between "#if RENDER" and "#endif" is the LED renderer, kept only
#   in the GUI build.
#   Print the solution on the first line of output, moves separated by
#   spaces, then exit through ecall 93: 0 if the solution replays to
#   solved, 1 if it does not, 2 if the input is not a valid cube.
#   asm/tables.s is appended after this file: pt4 and ot4 (.half,
#   [rank][4], slot 3 unused), pdbP and pdbO (.byte), src and tw (.byte,
#   source[] and twist[] of solver.c).
#
# Preprocessor pitfalls: a comment must not begin with a directive name
# right after the hash (if, define, include, ...) or with a number, which
# the preprocessor reads as a line marker.

.globl _start
.text
_start:
    # TODO: parse INPUT, rank it, search, print, validate
    li    a0, 2
    li    a7, 93
    ecall

.data
input:
    .string INPUT
