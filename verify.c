/* Host-only validation. Default: EVERY rank. Optional count: spread sample. */
#include <time.h>
#define main solver_cli_main
#include "solver.c"
#undef main
uint8_t *oracle_distances(void);
int oracle_replay(uint32_t rank, const uint8_t *path, unsigned length);
int main(int argc, char **argv)
{
    unsigned count = STATES;
    if (argc == 2) {
        char *end;
        unsigned long n = strtoul(argv[1], &end, 10);
        if (!*argv[1] || *end || n < 1 || n > STATES) return 2;
        count = (unsigned) n;
    } else if (argc != 1) return 2;
    uint8_t *distance = oracle_distances();
    if (!distance || !build_tables()) return 1;
    clock_t start = clock();
    for (unsigned i = 0; i < count; ++i) {
        uint32_t r = (uint32_t) ((uint64_t) i * STATES / count);
        uint16_t p = (uint16_t) (r / ORIENTATIONS), o = (uint16_t) (r % ORIENTATIONS);
        uint8_t path[MAX_SOLUTION];
        int n = solve_ida(p, o, path);
        if (lower_bound(p, o) > distance[r] || n != distance[r] ||
            !oracle_replay(r, path, (unsigned) n)) {
            fprintf(stderr, "FAIL rank=%u expected=%u got=%d\n", r, distance[r], n);
            free(distance); return 1;
        }
        if ((i + 1) % 100000 == 0) {
            fprintf(stderr, "%u/%u verified\n", i + 1, count);
            fflush(stderr);
        }
    }
    printf("PASS %u/%u states: admissible h, optimal length, original-model replay; %.3f CPU s\n",
           count, STATES, (double) (clock() - start) / CLOCKS_PER_SEC);
    printf("Tables: %zu bytes; DFS frames: %zu bytes; path: %u bytes\n",
           sizeof permutation + sizeof orientation + sizeof permutation_distance + sizeof orientation_distance,
           sizeof(frame_t) * (MAX_SOLUTION + 1), MAX_SOLUTION);
    free(distance);
    return output_failed();
}
