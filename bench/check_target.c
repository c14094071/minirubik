/* Host test of the target solver code in target/ida.h, compiled with the
 * same -DO2=... options as the Ripes build.
 *
 * For each state tested it checks that ida_parse accepts the 14-digit
 * string, that ida_rank agrees with rank_state in solver.c, that
 * ida_search returns a path of exactly the BFS distance, that ida_verify
 * accepts it, and that solver.c's own apply_move replays it to solved.
 * Invalid strings from the Makefile must be rejected by ida_parse.
 *
 * Usage: check_target sample   every distance-11 state and every 97th state
 *        check_target all      every state (gate H3 for the target code)
 *        check_target count STATE ...
 *                              operation counts of the search, when built
 *                              with -DIDA_STATS
 */
#include "model.h"
#include "../target/ida.h"

static int check_state(uint32_t rank)
{
    char s[15];
    state_t state;
    uint8_t pp[7], oo[7], path[IDA_MAXD];
    uint16_t p, o;
    unrank_state(rank, &state);
    for (int i = 0; i < CUBIES; ++i) {
        s[i] = (char) ('1' + state.p[i]);
        s[i + CUBIES] = (char) ('1' + state.o[i]);
    }
    s[14] = '\0';
    if (!ida_parse(s, pp, oo)) {
        printf("FAIL %s: rejected by ida_parse\n", s);
        return 0;
    }
    ida_rank(pp, oo, &p, &o);
    if ((uint32_t) p * ORIENTATIONS + o != rank) {
        printf("FAIL %s: ida_rank disagrees with rank_state\n", s);
        return 0;
    }
    int len = ida_search(p, o, path);
    if (len != dist[rank]) {
        printf("FAIL %s: length %d, BFS distance %u\n", s, len, dist[rank]);
        return 0;
    }
    if (!ida_verify(pp, oo, path, len)) {
        printf("FAIL %s: rejected by ida_verify\n", s);
        return 0;
    }
    for (int k = 0; k < len; ++k)
        state = apply_move(state, path[k]);
    if (rank_state(&state) != 0) {
        printf("FAIL %s: apply_move does not reach solved\n", s);
        return 0;
    }
    return 1;
}

#ifdef IDA_STATS
static int count_states(int argc, char **argv)
{
    printf("%-15s %4s %11s %11s %11s %11s %11s %11s\n", "state", "iter",
           "expansions", "generated", "pushes", "pdbP loads", "pdbO loads",
           "next checks");
    for (int i = 0; i < argc; ++i) {
        uint8_t pp[7], oo[7], path[IDA_MAXD];
        uint16_t p, o;
        if (!ida_parse(argv[i], pp, oo)) {
            fprintf(stderr, "count: invalid state '%s'\n", argv[i]);
            return 2;
        }
        ida_rank(pp, oo, &p, &o);
        memset(&ida_stats, 0, sizeof ida_stats);
        if (ida_search(p, o, path) < 0)
            return 1;
        printf("%-15s %4llu %11llu %11llu %11llu %11llu %11llu %11llu\n",
               argv[i], (unsigned long long) ida_stats.iterations,
               (unsigned long long) ida_stats.expansions,
               (unsigned long long) ida_stats.generated,
               (unsigned long long) ida_stats.pushes,
               (unsigned long long) ida_stats.pdbp_loads,
               (unsigned long long) ida_stats.pdbo_loads,
               (unsigned long long) ida_stats.next_checks);
    }
    return 0;
}
#endif

int main(int argc, char **argv)
{
    static const char *const invalid[] = {
        "1234567111111",   "123456711111111", "02345671111111",
        "82345671111111",  "12345671111110",  "12345671111114",
        "1234567111111a",  "11345671111111",  "12345671111112",
    };
#ifdef IDA_STATS
    if (argc >= 3 && !strcmp(argv[1], "count"))
        return count_states(argc - 2, argv + 2);
#endif
    int all = argc == 2 && !strcmp(argv[1], "all");
    if (argc != 2 || (!all && strcmp(argv[1], "sample"))) {
        fputs("usage: check_target sample | all | count STATE...\n", stderr);
        return 2;
    }
    if (!model_init()) {
        fputs("table construction failed\n", stderr);
        return 1;
    }
    uint32_t tested = 0, failed = 0;
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        uint8_t pp[7], oo[7];
        /* ida_parse reads 15 bytes; pad the short strings */
        char s[32] = {0};
        strncpy(s, invalid[i], sizeof s - 1);
        if (ida_parse(s, pp, oo)) {
            printf("FAIL %s: invalid input accepted\n", invalid[i]);
            ++failed;
        }
    }
    for (uint32_t rank = 0; rank < STATES; ++rank) {
        if (!all && dist[rank] != 11 && rank % 97)
            continue;
        ++tested;
        if (!check_state(rank) && ++failed > 10)
            break;
    }
    printf("O2=%d O3=%d O4=%d O5=%d O6=%d O7=%d: %lu states, %lu failures\n",
           O2, O3, O4, O5, O6, O7, (unsigned long) tested,
           (unsigned long) failed);
    return failed != 0;
}
