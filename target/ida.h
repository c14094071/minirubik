/* IDA* with D = max(B, C) and same-face pruning, written for RV32I.
 *
 * Freestanding: no libc, no recursion, no multiply, divide or remainder.
 * Shared by target/solve.c (the program run on Ripes) and by
 * bench/check_target.c (the host test of every variant).
 *
 * Stage 3 options, each 0 or 1 at compile time (O1, the explicit stack,
 * is always on because the assignment forbids recursion):
 *   O2  test a child's bound when it is generated, before pushing it
 *   O3  load the orientation PDB only if the permutation PDB alone
 *       does not prune
 *   O4  transition tables laid out [rank][4], indexed by shift and or
 *   O5  solved test as one comparison, (p | o) == 0
 *   O6  raise the bound by one per iteration instead of tracking the
 *       smallest f that exceeded it
 *   O7  branchless max
 */
#ifndef TARGET_IDA_H
#define TARGET_IDA_H

#include <stdint.h>

#ifndef O2
#define O2 0
#endif
#ifndef O3
#define O3 0
#endif
#ifndef O4
#define O4 0
#endif
#ifndef O5
#define O5 0
#endif
#ifndef O6
#define O6 0
#endif
#ifndef O7
#define O7 0
#endif

#include "tables.h"

#if O4
#define PT(face, x) pt4[x][face]
#define OT(face, x) ot4[x][face]
#else
#define PT(face, x) pt3[face][x]
#define OT(face, x) ot3[face][x]
#endif

#if O5
#define IDA_SOLVED(p, o) (((p) | (o)) == 0)
#else
#define IDA_SOLVED(p, o) ((p) == 0 && (o) == 0)
#endif

enum { IDA_MAXD = 12 }; /* the diameter is 11 */

/* Operation counts for Stage 3, compiled in only on the host with
 * -DIDA_STATS; on the target IDA_COUNT expands to nothing.
 */
#ifdef IDA_STATS
static struct {
    uint64_t iterations;  /* bounds tried */
    uint64_t expansions;  /* nodes whose moves were generated */
    uint64_t generated;   /* children produced by a transition lookup */
    uint64_t pushes;      /* children pushed onto the explicit stack */
    uint64_t pdbp_loads;  /* permutation PDB reads */
    uint64_t pdbo_loads;  /* orientation PDB reads */
    uint64_t next_checks; /* comparisons against the smallest exceeding f */
} ida_stats;
#define IDA_COUNT(field) (++ida_stats.field)
#else
#define IDA_COUNT(field) ((void) 0)
#endif

/* Face and number of quarter turns of each move, so that decoding a move
 * needs no divide: moves are R R2 R' B B2 B' D D2 D'.
 */
static const uint8_t ida_move_face[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
static const uint8_t ida_move_turns[9] = {1, 2, 3, 1, 2, 3, 1, 2, 3};

static inline uint8_t ida_max(uint8_t a, uint8_t b)
{
#if O7
    uint32_t mask = -(uint32_t) (a < b); /* all ones when b is larger */
    return (uint8_t) (a ^ ((a ^ b) & mask));
#else
    return a > b ? a : b;
#endif
}

static inline uint8_t ida_h(uint16_t p, uint16_t o)
{
    IDA_COUNT(pdbp_loads);
    IDA_COUNT(pdbo_loads);
    return ida_max(pdbP[p], pdbO[o]);
}

/* f = g + h for a node at depth g. Any result above bound means prune.
 * Under O3 a result above bound may be below the true f, which can only
 * lower the next bound; the search stays optimal.
 */
static inline uint8_t ida_f(uint8_t g, uint16_t p, uint16_t o, uint8_t bound)
{
#if O3
    uint8_t f = (uint8_t) (g + pdbP[p]);
    IDA_COUNT(pdbp_loads);
    if (f > bound)
        return f;
    IDA_COUNT(pdbo_loads);
    return (uint8_t) (g + pdbO[o]);
#else
    (void) bound;
    return (uint8_t) (g + ida_h(p, o));
#endif
}

/* Parses PPPPPPPOOOOOOO into cubie and twist arrays (0-based). Returns 1 if
 * the string is a valid cube: seven distinct cubies 1-7, twists 1-3, and a
 * twist sum divisible by 3.
 */
static int ida_parse(const char *s, uint8_t *pp, uint8_t *oo)
{
    uint32_t seen = 0, sum = 0;
    for (int i = 0; i < 7; ++i) {
        uint32_t c = (uint32_t) (s[i] - '1');
        uint32_t t = (uint32_t) (s[i + 7] - '1');
        if (c > 6 || t > 2 || (seen >> c & 1))
            return 0;
        seen |= 1u << c;
        pp[i] = (uint8_t) c;
        oo[i] = (uint8_t) t;
        sum += t;
    }
    while (sum >= 3) /* at most 14: four subtractions, no divide */
        sum -= 3;
    return s[14] == '\0' && sum == 0;
}

/* Lehmer rank by Horner's rule, p = p * (7 - i) + c_i, with each constant
 * multiply written as shifts and adds; o = o * 3 + twist the same way.
 */
static void ida_rank(const uint8_t *pp, const uint8_t *oo, uint16_t *p,
                     uint16_t *o)
{
    uint32_t c[7], r, q = 0;
    for (int i = 0; i < 7; ++i) {
        c[i] = 0;
        for (int j = i + 1; j < 7; ++j)
            c[i] += pp[j] < pp[i];
    }
    r = c[0];
    r = (r << 2) + (r << 1) + c[1]; /* x6 */
    r = (r << 2) + r + c[2];        /* x5 */
    r = (r << 2) + c[3];            /* x4 */
    r = (r << 1) + r + c[4];        /* x3 */
    r = (r << 1) + c[5];            /* x2 */
    r = r + c[6];                   /* x1 */
    for (int i = 0; i < 6; ++i)
        q = (q << 1) + q + oo[i]; /* x3 */
    *p = (uint16_t) r;
    *o = (uint16_t) q;
}

/* IDA*. Writes the moves to path and returns their number, or -1. */
static int ida_search(uint16_t p0, uint16_t o0, uint8_t *path)
{
    uint16_t sp[IDA_MAXD + 1], so[IDA_MAXD + 1];
    uint16_t cp[IDA_MAXD + 1], co[IDA_MAXD + 1];
    uint8_t face[IDA_MAXD + 1], turn[IDA_MAXD + 1];
    uint8_t bound = ida_h(p0, o0);
    int g;

    if (IDA_SOLVED(p0, o0))
        return 0;
    for (;;) {
#if !O6
        uint8_t next = UINT8_MAX;
#endif
        IDA_COUNT(iterations);
        g = 0;
        sp[0] = p0;
        so[0] = o0;
        goto expand;

    enter: /* the node at depth g was just pushed */
#if !O2
    {
        uint8_t f = ida_f((uint8_t) g, sp[g], so[g], bound);
        if (f > bound) {
#if !O6
            IDA_COUNT(next_checks);
            if (f < next)
                next = f;
#endif
            --g;
            goto next_move;
        }
    }
#endif
        if (IDA_SOLVED(sp[g], so[g]))
            return g;

    expand:
        IDA_COUNT(expansions);
        face[g] = 0;
        turn[g] = 0;
        cp[g] = sp[g];
        co[g] = so[g];

    next_move:
        for (;;) {
            uint8_t fc = face[g];
            /* next face when this one is used up, or when it was turned on
             * the previous move (same-face pruning)
             */
            if (turn[g] == 3 || (g > 0 && fc == face[g - 1])) {
                if (++fc == 3)
                    break;
                face[g] = fc;
                turn[g] = 0;
                cp[g] = sp[g];
                co[g] = so[g];
                continue;
            }
            cp[g] = PT(fc, cp[g]);
            co[g] = OT(fc, co[g]);
            IDA_COUNT(generated);
            path[g] = (uint8_t) ((fc << 1) + fc + turn[g]); /* fc*3 + turn */
            ++turn[g];
#if O2
            {
                uint8_t f = ida_f((uint8_t) (g + 1), cp[g], co[g], bound);
                if (f > bound) {
#if !O6
                    IDA_COUNT(next_checks);
                    if (f < next)
                        next = f;
#endif
                    continue;
                }
            }
#endif
            IDA_COUNT(pushes);
            sp[g + 1] = cp[g];
            so[g + 1] = co[g];
            ++g;
            goto enter;
        }
        /* every move at depth g tried: back up */
        if (g > 0) {
            --g;
            goto next_move;
        }
#if O6
        ++bound;
        if (bound > IDA_MAXD)
            return -1;
#else
        if (next == UINT8_MAX)
            return -1;
        bound = next;
#endif
    }
}

/* Gate T5 in the program: replays path on the cubie arrays with source[]
 * and twist[], independently of the rank tables, and checks that the cube
 * ends solved. Twists are reduced modulo 3 by one conditional subtract,
 * since the sum of two twists is at most 4.
 */
static int ida_verify(const uint8_t *pp0, const uint8_t *oo0,
                      const uint8_t *path, int len)
{
    uint8_t pp[7], oo[7], np[7], no[7];
    for (int i = 0; i < 7; ++i) {
        pp[i] = pp0[i];
        oo[i] = oo0[i];
    }
    for (int k = 0; k < len; ++k) {
        const uint8_t *s = src[ida_move_face[path[k]]];
        const uint8_t *t = tw[ida_move_face[path[k]]];
        for (int n = ida_move_turns[path[k]]; n > 0; --n) {
            for (int i = 0; i < 7; ++i) {
                uint32_t twist = (uint32_t) oo[s[i]] + t[i];
                if (twist >= 3)
                    twist -= 3;
                np[i] = pp[s[i]];
                no[i] = (uint8_t) twist;
            }
            for (int i = 0; i < 7; ++i) {
                pp[i] = np[i];
                oo[i] = no[i];
            }
        }
    }
    for (int i = 0; i < 7; ++i)
        if (pp[i] != i || oo[i] != 0)
            return 0;
    return 1;
}

#endif
