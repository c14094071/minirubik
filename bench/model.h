/* Shared host-side model for the bench tools.
 *
 * Includes solver.c unchanged, with its main() renamed, and builds from it
 * the factored transition tables, the exact distance of every state, and
 * the two pattern databases.
 */
#ifndef BENCH_MODEL_H
#define BENCH_MODEL_H

#define main solver_main
#include "../solver.c"
#undef main

static uint16_t ptab[3][PERMUTATIONS], otab[3][ORIENTATIONS];
static uint8_t dist[STATES];
static uint8_t pdb_p[PERMUTATIONS], pdb_o[ORIENTATIONS];

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

/* Builds every table; returns 0 if any search fails to cover its space. */
static int model_init(void)
{
    build_transitions();
    return bfs_full() && bfs_pdb(pdb_p, PERMUTATIONS, &ptab[0][0]) &&
           bfs_pdb(pdb_o, ORIENTATIONS, &otab[0][0]);
}

#endif
