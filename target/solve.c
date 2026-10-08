/* The C solver as it runs on Ripes: the reference that the hand-written
 * RV32I assembly has to beat.
 *
 * Build (see target/measure.sh):
 *   riscv64-unknown-elf-gcc -O2 -march=rv32i -mabi=ilp32 -nostdlib \
 *       -ffreestanding -DSTATE='"21345671111111"' [-DO2=1 ...] solve.c
 *
 * -nostdlib leaves out libgcc as well, so a multiply or divide that the
 * compiler cannot turn into shifts fails to link instead of silently
 * calling __mulsi3 or __divsi3.
 *
 * Prints the solution as "B' R' D2 ...", then exits through ecall 93 with
 * 0 if the solution replays to the solved cube (gate T5), 1 if it does not,
 * and 2 if the input is not a valid cube.
 */
#include "ida.h"

#ifndef STATE
#define STATE "21345671111111"
#endif

static const char input[] = STATE;

static const char *const move_names[9] = {"R",  "R2", "R'", "B", "B2",
                                          "B'", "D",  "D2", "D'"};

/* Ripes environment calls: a7 = 4 prints a string, 11 a character,
 * 93 exits with the status in a0.
 */
static void print_string(const char *s)
{
    register const char *a0 __asm__("a0") = s;
    register int a7 __asm__("a7") = 4;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void print_char(int c)
{
    register int a0 __asm__("a0") = c;
    register int a7 __asm__("a7") = 11;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
}

static void __attribute__((noreturn)) exit_with(int status)
{
    register int a0 __asm__("a0") = status;
    register int a7 __asm__("a7") = 93;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7) : "memory");
    for (;;)
        ;
}

void __attribute__((noreturn)) _start(void)
{
    uint8_t pp[7], oo[7], path[IDA_MAXD];
    uint16_t p, o;

    if (!ida_parse(input, pp, oo))
        exit_with(2);
    ida_rank(pp, oo, &p, &o);
    int len = ida_search(p, o, path);
    if (len < 0)
        exit_with(1);
    for (int k = 0; k < len; ++k) {
        if (k)
            print_char(' ');
        print_string(move_names[path[k]]);
    }
    print_char('\n');
    exit_with(ida_verify(pp, oo, path, len) ? 0 : 1);
}
