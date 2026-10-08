/* Host-side comparison of IDA* heuristics for the 2x2x2 cube.
 *
 * The move model, ranking and validation are taken unchanged from solver.c,
 * whose own main() is renamed so that this file can provide its own.
 *
 * Candidates (see the Stage 2 section of the HackMD note):
 *   A  h = 0, brute-force iterative deepening
 *   B  orientation pattern database (729 entries)
 *   C  permutation pattern database (5,040 entries)
 *   D  max(B, C)
 *
 * Usage:
 *   compare check                       correctness test on tests/solutions.txt
 *   compare stats                       max h, mean h and gate H1 for A-D
 *   compare gates                       gate H2: every table complete and sane
 *   compare search CAND [--prune] all11 every distance-11 state
 *   compare search CAND [--prune] all   every state (gate H3)
 *   compare search CAND [--prune] STATE ...
 *
 * --prune skips a turn of the face turned on the previous move.
 */
#define main solver_main
#include "../solver.c"
#undef main

#include <time.h>

enum { NODE_COST = 200 }; /* instructions per expansion assumed by the spec */

static uint16_t ptab[3][PERMUTATIONS], otab[3][ORIENTATIONS];
static uint8_t dist[STATES];
static uint8_t pdb_p[PERMUTATIONS], pdb_o[ORIENTATIONS];

static char cand = 'D';
static int prune_same_face;
static uint64_t expanded, generated;
static uint8_t path[16], solution_len, next_bound;

/* Same loops as build_table() in solver.c. */
static void build_transitions(void)
{
    state_t state;
    for (uint16_t rank = 0; rank < PERMUTATIONS; ++rank) {
        unrank_state((uint32_t) rank * ORIENTATIONS, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            ptab[face][rank] = (uint16_t) (rank_state(&next) / ORIENTATIONS);
        }
    }
    for (uint16_t rank = 0; rank < ORIENTATIONS; ++rank) {
        unrank_state(rank, &state);
        for (uint8_t face = 0; face < 3; ++face) {
            state_t next = quarter_turn(state, face);
            otab[face][rank] = (uint16_t) (rank_state(&next) % ORIENTATIONS);
        }
    }
}

/* Exact distance of every state: the reference for gates H1 and H3. */
static int bfs_full(void)
{
    uint32_t *queue = malloc((size_t) STATES * sizeof *queue);
    uint32_t head = 0, tail = 1;
    if (!queue)
        return 0;
    memset(dist, UINT8_MAX, sizeof dist);
    dist[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint32_t here = queue[head++];
        uint16_t p = (uint16_t) (here / ORIENTATIONS);
        uint16_t o = (uint16_t) (here % ORIENTATIONS);
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t np = p, no = o;
            for (uint8_t turn = 0; turn < 3; ++turn) {
                np = ptab[face][np];
                no = otab[face][no];
                uint32_t there = (uint32_t) np * ORIENTATIONS + no;
                if (dist[there] == UINT8_MAX) {
                    dist[there] = (uint8_t) (dist[here] + 1);
                    queue[tail++] = there;
                }
            }
        }
    }
    free(queue);
    return tail == STATES;
}

/* Exact distances in one abstract puzzle, by BFS from its solved state 0.
 * tab points at a 3 x n transition table.
 */
static int bfs_pdb(uint8_t *pdb, uint16_t n, const uint16_t *tab)
{
    uint16_t queue[PERMUTATIONS], head = 0, tail = 1;
    memset(pdb, UINT8_MAX, n);
    pdb[0] = 0;
    queue[0] = 0;
    while (head < tail) {
        uint16_t here = queue[head++];
        for (uint8_t face = 0; face < 3; ++face) {
            uint16_t next = here;
            for (uint8_t turn = 0; turn < 3; ++turn) {
                next = tab[face * n + next];
                if (pdb[next] == UINT8_MAX) {
                    pdb[next] = (uint8_t) (pdb[here] + 1);
                    queue[tail++] = next;
                }
            }
        }
    }
    return tail == n;
}

static uint8_t heuristic(uint16_t p, uint16_t o)
{
    switch (cand) {
    case 'A':
        return 0;
    case 'B':
        return pdb_o[o];
    case 'C':
        return pdb_p[p];
    default:
        return pdb_p[p] > pdb_o[o] ? pdb_p[p] : pdb_o[o];
    }
}

static int dfs(uint16_t p, uint16_t o, uint8_t g, uint8_t bound, int last_face)
{
    uint8_t f = (uint8_t) (g + heuristic(p, o));
    if (f > bound) {
        if (f < next_bound)
            next_bound = f;
        return 0;
    }
    if (p == 0 && o == 0) {
        solution_len = g;
        return 1;
    }
    ++expanded;
    for (int face = 0; face < 3; ++face) {
        if (prune_same_face && face == last_face)
            continue;
        uint16_t np = p, no = o;
        for (int turn = 0; turn < 3; ++turn) {
            np = ptab[face][np];
            no = otab[face][no];
            ++generated;
            path[g] = (uint8_t) (face * 3 + turn);
            if (dfs(np, no, (uint8_t) (g + 1), bound, face))
                return 1;
        }
    }
    return 0;
}

/* IDA*: returns 1 and sets solution_len/path[] on success. */
static int ida(uint32_t rank)
{
    uint16_t p = (uint16_t) (rank / ORIENTATIONS);
    uint16_t o = (uint16_t) (rank % ORIENTATIONS);
    uint8_t bound = heuristic(p, o);
    expanded = generated = 0;
    for (;;) {
        next_bound = UINT8_MAX;
        if (dfs(p, o, 0, bound, -1))
            return 1;
        if (next_bound == UINT8_MAX)
            return 0;
        bound = next_bound;
    }
}

/* Gates T5/H3 on the host: the path solves the state and is optimal. */
static int solution_ok(uint32_t rank)
{
    state_t state;
    unrank_state(rank, &state);
    for (uint8_t i = 0; i < solution_len; ++i)
        state = apply_move(state, path[i]);
    return rank_state(&state) == 0 && solution_len == dist[rank];
}

static void format_state(uint32_t rank, char out[15])
{
    state_t state;
    unrank_state(rank, &state);
    for (int i = 0; i < CUBIES; ++i) {
        out[i] = (char) ('1' + state.p[i]);
        out[i + CUBIES] = (char) ('1' + state.o[i]);
    }
    out[14] = '\0';
}

static double seconds_since(clock_t start)
{
    return (double) (clock() - start) / CLOCKS_PER_SEC;
}

static int cmd_check(void)
{
    static const struct {
        const char *state;
        uint8_t length;
    } vectors[] = {
        {"12345671111111", 0},  {"62345713133111", 8}, {"24316572122213", 8},
        {"25713642221111", 8},  {"24513763133333", 9}, {"43752611332133", 9},
        {"25416373331111", 10}, {"21345671111111", 11},
    };
    static const char cands[] = "ABCD";
    int failures = 0;
    for (size_t v = 0; v < sizeof vectors / sizeof vectors[0]; ++v) {
        state_t state;
        if (!parse_state(vectors[v].state, &state))
            return 1;
        uint32_t rank = rank_state(&state);
        if (dist[rank] != vectors[v].length) {
            printf("FAIL %s: BFS distance %u, expected %u\n", vectors[v].state,
                   dist[rank], vectors[v].length);
            ++failures;
        }
        for (int c = 0; c < 4; ++c) {
            cand = cands[c];
            if (cand == 'A' && vectors[v].length > 8)
                continue; /* brute force at depth 9+ is too slow for a test */
            if (!ida(rank) || !solution_ok(rank)) {
                printf("FAIL %s with candidate %c\n", vectors[v].state, cand);
                ++failures;
            }
        }
    }
    puts(failures ? "check FAILED" : "check passed");
    return failures != 0;
}

static int cmd_stats(void)
{
    static const char cands[] = "ABCD";
    printf("%-9s %5s %8s %6s\n", "candidate", "max h", "mean h", "H1");
    for (int c = 0; c < 4; ++c) {
        uint8_t max = 0;
        uint64_t sum = 0, violations = 0;
        cand = cands[c];
        for (uint32_t rank = 0; rank < STATES; ++rank) {
            uint8_t h = heuristic((uint16_t) (rank / ORIENTATIONS),
                                  (uint16_t) (rank % ORIENTATIONS));
            if (h > max)
                max = h;
            sum += h;
            violations += h > dist[rank];
        }
        printf("%-9c %5u %8.3f %6s", cand, max, (double) sum / STATES,
               violations ? "FAIL" : "pass");
        if (violations)
            printf("  (%llu states with h > d)", (unsigned long long) violations);
        putchar('\n');
    }
    return 0;
}

/* Gate H2 for one transition table: each face maps the n ranks one-to-one
 * onto themselves, and four quarter turns of a face restore every rank.
 */
static int transitions_ok(const uint16_t *tab, uint16_t n)
{
    static uint8_t seen[PERMUTATIONS];
    for (uint8_t face = 0; face < 3; ++face) {
        memset(seen, 0, n);
        for (uint16_t x = 0; x < n; ++x) {
            uint16_t y = tab[face * n + x];
            if (y >= n || seen[y]++)
                return 0;
            for (int turn = 1; turn < 4; ++turn)
                y = tab[face * n + y];
            if (y != x)
                return 0;
        }
    }
    return 1;
}

/* Gate H2 for one distance table: every entry filled, the solved entry 0 and
 * the only 0. Prints a histogram; returns the maximum, or -1 on failure.
 */
static int report_distances(const char *name, const uint8_t *table, uint32_t n)
{
    uint32_t hist[16] = {0};
    int max = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (table[i] > 15) {
            printf("%s: entry %lu not filled\n", name, (unsigned long) i);
            return -1;
        }
        ++hist[table[i]];
        if (table[i] > max)
            max = table[i];
    }
    printf("%-22s %8lu entries, max %2d, solved entry %u, zeros %lu\n", name,
           (unsigned long) n, max, table[0], (unsigned long) hist[0]);
    printf("  by distance:");
    for (int d = 0; d <= max; ++d)
        printf(" %lu", (unsigned long) hist[d]);
    putchar('\n');
    return table[0] == 0 && hist[0] == 1 ? max : -1;
}

static int cmd_gates(void)
{
    /* Distance distribution from report.md, section 4. */
    static const uint32_t expected[12] = {1,      9,      54,      321,
                                          1847,   9992,   50136,   227536,
                                          870072, 1887748, 623800, 2644};
    uint32_t hist[12] = {0};
    int ok = 1;
    int pt = transitions_ok(&ptab[0][0], PERMUTATIONS);
    int ot = transitions_ok(&otab[0][0], ORIENTATIONS);
    printf("permutation transitions 3 x %d, each face a bijection of order 4: "
           "%s\n",
           PERMUTATIONS, pt ? "pass" : "FAIL");
    printf("orientation transitions 3 x %d, each face a bijection of order 4: "
           "%s\n",
           ORIENTATIONS, ot ? "pass" : "FAIL");
    ok = pt && ot;
    int dmax = report_distances("exact distances (BFS)", dist, STATES);
    int bmax = report_distances("B: orientation PDB", pdb_o, ORIENTATIONS);
    int cmax = report_distances("C: permutation PDB", pdb_p, PERMUTATIONS);
    ok = ok && dmax == 11 && bmax >= 0 && cmax >= 0;
    for (uint32_t rank = 0; rank < STATES; ++rank)
        if (dist[rank] < 12)
            ++hist[dist[rank]];
    int match = !memcmp(hist, expected, sizeof hist);
    printf("exact distances match report.md section 4: %s\n",
           match ? "pass" : "FAIL");
    ok = ok && match;
    puts("packed tables: none, every table uses one byte per entry "
         "(gate H4 not applicable)");
    puts(ok ? "gate H2 passed" : "gate H2 FAILED");
    return !ok;
}

/* Search every state (range 1) or every distance-11 state (range 11). */
static int search_range(int range)
{
    uint64_t total[12] = {0}, worst[12] = {0};
    uint32_t count[12] = {0}, worst_rank[12] = {0}, done = 0;
    clock_t start = clock();
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        uint8_t d = dist[rank];
        if (range == 11 && d != 11)
            continue;
        if (!ida(rank) || !solution_ok(rank)) {
            char s[15];
            format_state(rank, s);
            printf("FAIL: wrong or suboptimal solution for %s\n", s);
            return 1;
        }
        ++count[d];
        total[d] += expanded;
        if (expanded > worst[d]) {
            worst[d] = expanded;
            worst_rank[d] = rank;
        }
        if (range == 1 && ++done % (STATES / 10) == 0)
            fprintf(stderr, "  %lu%% done, %.0f s\n",
                    (unsigned long) ((uint64_t) done * 100 / STATES),
                    seconds_since(start));
    }
    double elapsed = seconds_since(start);
    const char *pruning = prune_same_face ? " (same-face pruning)" : "";
    char s[15];
    if (range == 11) {
        format_state(worst_rank[11], s);
        printf("candidate %c%s, %lu distance-11 states, all optimal\n", cand,
               pruning, (unsigned long) count[11]);
        printf("  mean expansions  %.1f\n", (double) total[11] / count[11]);
        printf("  worst expansions %llu  (state %s)\n",
               (unsigned long long) worst[11], s);
        printf("  est. instructions, worst state  %llu  (x%d per expansion)\n",
               (unsigned long long) (worst[11] * NODE_COST), NODE_COST);
        printf("  wall-clock  %.2f s\n", elapsed);
        return 0;
    }
    uint64_t sum = 0, max = 0;
    uint32_t states = 0, max_rank = 0;
    printf("candidate %c%s, every state\n", cand, pruning);
    printf("   d    states  mean expansions  worst expansions  worst state\n");
    for (int d = 0; d < 12; ++d) {
        format_state(worst_rank[d], s);
        printf("  %2d %9lu %16.1f %17llu  %s\n", d, (unsigned long) count[d],
               count[d] ? (double) total[d] / count[d] : 0.0,
               (unsigned long long) worst[d], s);
        states += count[d];
        sum += total[d];
        if (worst[d] > max) {
            max = worst[d];
            max_rank = worst_rank[d];
        }
    }
    format_state(max_rank, s);
    printf("  all %8lu %16.1f %17llu  %s\n", (unsigned long) states,
           (double) sum / states, (unsigned long long) max, s);
    printf("  est. instructions, worst state  %llu  (x%d per expansion)\n",
           (unsigned long long) (max * NODE_COST), NODE_COST);
    printf("  wall-clock  %.2f s\n", elapsed);
    printf("gate H3 %s: %lu of %d states solved in exactly their BFS "
           "distance\n",
           states == STATES ? "passed" : "FAILED", (unsigned long) states,
           STATES);
    return states != STATES;
}

static int cmd_search(int argc, char **argv)
{
    int i = 0, range = 0;
    if (argc < 1 || !strchr("ABCD", argv[0][0]) || argv[0][1]) {
        fputs("search: candidate must be one of A B C D\n", stderr);
        return 2;
    }
    cand = argv[i++][0];
    if (i < argc && !strcmp(argv[i], "--prune")) {
        prune_same_face = 1;
        ++i;
    }
    if (i < argc && !strcmp(argv[i], "all11"))
        range = 11;
    else if (i < argc && !strcmp(argv[i], "all"))
        range = 1;
    if (i >= argc) {
        fputs("search: give all, all11 or one or more 14-digit states\n",
              stderr);
        return 2;
    }
    if (range && (cand == 'A' || (cand == 'B' && range == 1))) {
        fputs("search: that run would take days; pass a few states instead\n",
              stderr);
        return 2;
    }
    if (range)
        return search_range(range);
    for (; i < argc; ++i) {
        state_t state;
        if (!parse_state(argv[i], &state)) {
            fprintf(stderr, "search: invalid state '%s'\n", argv[i]);
            return 2;
        }
        uint32_t rank = rank_state(&state);
        clock_t start = clock();
        if (!ida(rank) || !solution_ok(rank)) {
            printf("FAIL: wrong or suboptimal solution for %s\n", argv[i]);
            return 1;
        }
        printf("candidate %c%s  %s  d=%u  expanded %llu  generated %llu  "
               "est. instructions %llu  %.2f s\n  solution:",
               cand, prune_same_face ? " (pruned)" : "", argv[i], dist[rank],
               (unsigned long long) expanded, (unsigned long long) generated,
               (unsigned long long) (expanded * NODE_COST),
               seconds_since(start));
        for (uint8_t m = 0; m < solution_len; ++m)
            printf(" %s", move_names[path[m]]);
        putchar('\n');
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fputs("usage: compare check | stats | gates | search CAND [--prune] "
              "all|all11|STATE...\n",
              stderr);
        return 2;
    }
    build_transitions();
    if (!bfs_full() || !bfs_pdb(pdb_p, PERMUTATIONS, &ptab[0][0]) ||
        !bfs_pdb(pdb_o, ORIENTATIONS, &otab[0][0])) {
        fputs("table construction failed\n", stderr);
        return 1;
    }
    if (!strcmp(argv[1], "check"))
        return cmd_check();
    if (!strcmp(argv[1], "stats"))
        return cmd_stats();
    if (!strcmp(argv[1], "gates"))
        return cmd_gates();
    if (!strcmp(argv[1], "search"))
        return cmd_search(argc - 2, argv + 2);
    fprintf(stderr, "unknown command '%s'\n", argv[1]);
    return 2;
}
