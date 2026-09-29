/* Host-only validation. Default: EVERY rank. Optional count: spread sample.
 * Put beside solver.c, solver_opt.c, oracle.c and solver_original.c.
 * Baseline: gcc -O3 -std=c99 -DVERIFY_OPTIMIZED=0 verify.c oracle.c -o verify_before
 * Optimized: gcc -O3 -std=c99 -DVERIFY_OPTIMIZED=1 verify.c oracle.c -o verify_after
 * Both solvers must expose the same IDA* interface; oracle.c stays unchanged.
 * Use the accompanying solver.c/solver_opt.c containing COUNT/MEASURE hooks.
 * No additional header or test program is required.
 * Optional search count: ./verify_before 10000 or ./verify_after 10000.
 * No count argument means exhaustive search validation.
 * Reports five source-level search counters, NOT RV32I instruction counts.
 */
#include <inttypes.h>
#include <time.h>
#define SEARCH_STATS
#ifndef VERIFY_OPTIMIZED
#define VERIFY_OPTIMIZED 1
#endif
#define main solver_cli_main
#if VERIFY_OPTIMIZED
#include "solver_opt.c"
#else
#include "solver.c"
#endif
#undef main

uint8_t *oracle_distances(void);
int oracle_replay(uint32_t rank, const uint8_t *path, unsigned length);

/* 獨立確認之理論預期直徑 (HTM Metric) */
enum {
    EXPECTED_MAX_PERM = 7,         /* 7! = 5040 排列子空間直徑 */
    EXPECTED_MAX_ORIENT = 6,       /* 3^6 = 729 朝向子空間直徑 */
    EXPECTED_MAX_ORACLE = 11       /* 全狀態圖直徑 (God's Number) */
};

/* 逐項驗證 permutation 與 orientation 轉移表的一致性、雙射性與群論性質 */
static int verify_transition_tables(void)
{
    state_t state;

    /* 1. 驗證 Permutation 轉移表 (3 面 x 5040 排列) */
    for (uint8_t face = 0; face < 3; ++face) {
        uint8_t seen[PERMUTATIONS] = {0};

        for (uint16_t rank = 0; rank < PERMUTATIONS; ++rank) {
            unrank_state((uint32_t) rank * ORIENTATIONS, &state);
            state_t next = quarter_turn(state, face);
            uint16_t expected = (uint16_t) (rank_state(&next) / ORIENTATIONS);

            /* (1) 逐項與原始模型重算結果比對 */
            if (permutation[face][rank] != expected) {
                fprintf(stderr, "FAIL: permutation mismatch at face %u, rank %u (got %u, expected %u)\n",
                        face, rank, permutation[face][rank], expected);
                return 0;
            }

            /* (2) 雙射性檢驗：同一面轉動不得映射至相同 rank */
            if (seen[expected]) {
                fprintf(stderr, "FAIL: permutation non-bijective at face %u, duplicate target %u\n",
                        face, expected);
                return 0;
            }
            seen[expected] = 1;

            /* (3) 群論階數檢驗：連續轉動同一面 4 次必為恆等映射 (M^4 == I) */
            uint16_t p = rank;
            for (int t = 0; t < 4; ++t)
                p = permutation[face][p];
            if (p != rank) {
                fprintf(stderr, "FAIL: permutation order-4 violation at face %u, rank %u\n", face, rank);
                return 0;
            }
        }
    }

    /* 2. 驗證 Orientation 轉移表 (3 面 x 729 朝向) */
    for (uint8_t face = 0; face < 3; ++face) {
        uint8_t seen[ORIENTATIONS] = {0};

        for (uint16_t rank = 0; rank < ORIENTATIONS; ++rank) {
            unrank_state(rank, &state);
            state_t next = quarter_turn(state, face);
            uint16_t expected = (uint16_t) (rank_state(&next) % ORIENTATIONS);

            /* (1) 逐項與原始模型重算結果比對 */
            if (orientation[face][rank] != expected) {
                fprintf(stderr, "FAIL: orientation mismatch at face %u, rank %u (got %u, expected %u)\n",
                        face, rank, orientation[face][rank], expected);
                return 0;
            }

            /* (2) 雙射性檢驗 */
            if (seen[expected]) {
                fprintf(stderr, "FAIL: orientation non-bijective at face %u, duplicate target %u\n",
                        face, expected);
                return 0;
            }
            seen[expected] = 1;

            /* (3) 群論階數檢驗 (M^4 == I) */
            uint16_t o = rank;
            for (int t = 0; t < 4; ++t)
                o = orientation[face][o];
            if (o != rank) {
                fprintf(stderr, "FAIL: orientation order-4 violation at face %u, rank %u\n", face, rank);
                return 0;
            }
        }
    }

    return 1;
}

int main(int argc, char **argv)
{
    unsigned count = STATES;
    
    if (argc == 2) {
        char *end;
        unsigned long n = strtoul(argv[1], &end, 10);

        if (!*argv[1] || *end || n < 1 || n > STATES) return 2;
        count = (unsigned) n;
    } 
    else if (argc != 1) 
        return 2;
    
    printf("Testing: %s\n", VERIFY_OPTIMIZED ? "solver_opt.c" : "solver.c");
    printf("Search coverage: %u/%u states (%s)\n", count, STATES,
           count == STATES ? "exhaustive" : "deterministic sample");
    uint8_t *distance = oracle_distances();
    
    if (!distance || !build_tables()) {
        fputs("FAIL: oracle or abstract table construction\n", stderr);
        free(distance);
        return 1;
    }

    /* ==============================================================
     * 1. 逐項核對 Permutation 與 Orientation 轉移表
     * ============================================================== */
    if (!verify_transition_tables()) {
        free(distance);
        return 1;
    }
    printf("Transition Tables: PASS (Exact model match, Bijective, Order-4 verified)\n");

    /* ==============================================================
     * 2. 驗證 Solved state 對應的 entry (rank 0 步數必須為 0)
     * ============================================================== */
    if (permutation_distance[0] != 0 || orientation_distance[0] != 0 || distance[0] != 0) {
        fprintf(stderr, "FAIL: solved state entry is not 0 (P=%u, O=%u, Oracle=%u)\n",
                permutation_distance[0], orientation_distance[0], distance[0]);
        free(distance);
        return 1;
    }

    uint8_t solved_path[MAX_SOLUTION];
    int solved_steps = solve_ida(0, 0, solved_path);
    if (solved_steps != 0) {
        fprintf(stderr, "FAIL: solve_ida on solved state returned %d steps (expected 0)\n", solved_steps);
        free(distance);
        return 1;
    }

    /* ==============================================================
     * 3. 驗證距離表完整填入 (無 UINT8_MAX) 並與獨立預期直徑比對
     * ============================================================== */
    uint8_t max_perm = 0;
    for (unsigned i = 0; i < PERMUTATIONS; ++i) {
        if (i != 0 && permutation_distance[i] == 0) {
            fprintf(stderr, "FAIL: non-goal permutation has zero distance at %u\n", i);
            free(distance);
            return 1;
        }
        if (permutation_distance[i] == UINT8_MAX) {
            fprintf(stderr, "FAIL: permutation_distance incomplete at index %u\n", i);
            free(distance);
            return 1;
        }
        if (permutation_distance[i] > max_perm)
            max_perm = permutation_distance[i];
    }
    if (max_perm != EXPECTED_MAX_PERM) {
        fprintf(stderr, "FAIL: unexpected max_perm %u (expected %u)\n",
                max_perm, EXPECTED_MAX_PERM);
        free(distance);
        return 1;
    }

    uint8_t max_orient = 0;
    for (unsigned i = 0; i < ORIENTATIONS; ++i) {
        if (i != 0 && orientation_distance[i] == 0) {
            fprintf(stderr, "FAIL: non-goal orientation has zero distance at %u\n", i);
            free(distance);
            return 1;
        }
        if (orientation_distance[i] == UINT8_MAX) {
            fprintf(stderr, "FAIL: orientation_distance incomplete at index %u\n", i);
            free(distance);
            return 1;
        }
        if (orientation_distance[i] > max_orient)
            max_orient = orientation_distance[i];
    }
    if (max_orient != EXPECTED_MAX_ORIENT) {
        fprintf(stderr, "FAIL: unexpected max_orient %u (expected %u)\n",
                max_orient, EXPECTED_MAX_ORIENT);
        free(distance);
        return 1;
    }

    uint8_t max_oracle = 0;
    for (uint32_t i = 0; i < STATES; ++i) {
        if (distance[i] == UINT8_MAX) {
            fprintf(stderr, "FAIL: oracle distance table incomplete at rank %u\n", i);
            free(distance);
            return 1;
        }
        if (distance[i] > max_oracle)
            max_oracle = distance[i];
    }
    if (max_oracle != EXPECTED_MAX_ORACLE) {
        fprintf(stderr, "FAIL: unexpected oracle diameter %u (expected %u)\n",
                max_oracle, EXPECTED_MAX_ORACLE);
        free(distance);
        return 1;
    }

    printf("Distance Tables: PASS (All filled; Solved entry = 0; Max P = %u, Max O = %u, Diameter = %u)\n",
           max_perm, max_orient, max_oracle);
    puts("H2: PASS (including zero-distance uniqueness in both abstractions)");

    /* Check H1 exhaustively even if the more expensive search is sampled.
     * This scan is outside the search-validation timer. */
    for (uint32_t r = 0; r < STATES; ++r) {
        if (lower_bound((uint16_t) (r / ORIENTATIONS),
                        (uint16_t) (r % ORIENTATIONS)) > distance[r]) {
            fprintf(stderr, "FAIL H1 rank=%u\n", r);
            free(distance);
            return 1;
        }
    }
    puts("H1: PASS all 3674160 states");
    /* These two current solvers use unpacked arrays. Revisit if packing changes. */
    puts("H4: N/A (current distance arrays are unpacked)");

    /* ==============================================================
     * 4. IDA* 抽樣 / 全狀態驗證迴圈 (驗證下界可採納性與求解最優性)
     * ============================================================== */
    /* Exclude the earlier solved-state check, table building and H1 scan.
     * Accumulate all solve_ida calls in this corpus; do not reset per state.
     * lower_bound calls in verifier code are not instrumented. */
    search_stats = (search_stats_t) {0};
    clock_t start = clock();
    
    for (unsigned i = 0; i < count; ++i) {
        uint32_t r = (uint32_t) ((uint64_t) i * STATES / count);
        uint16_t p = (uint16_t) (r / ORIENTATIONS), o = (uint16_t) (r % ORIENTATIONS);
        uint8_t path[MAX_SOLUTION];
        int n = solve_ida(p, o, path);
        
        if (n < 0 || n > MAX_SOLUTION || n != distance[r] ||
            !oracle_replay(r, path, (unsigned) n)) {
            fprintf(stderr, "FAIL rank=%u expected=%u got=%d\n", r, distance[r], n);
            free(distance);
            return 1;
        }
        
        if ((i + 1) % 100000 == 0) {
            fprintf(stderr, "%u/%u verified\n", i + 1, count);
            fflush(stderr);
        }
    }
    
    printf("PASS %u/%u states: optimal length, original-model replay; %.3f host clock seconds\n",
           count, STATES, (double) (clock() - start) / CLOCKS_PER_SEC);
    puts(count == STATES ? "H3: PASS all states" :
         "H3: SAMPLE PASSED ONLY; run without a count for complete H3");
    puts("Time includes counters, search, correctness checks and replay; not a speedup benchmark.");
    puts("Search operation counts (whole corpus; C source-level events):");
    printf("goal_tests: %" PRIu64 "\n", search_stats.goal_tests);
    printf("div3_evaluations: %" PRIu64 "\n", search_stats.div3_evaluations);
    printf("mod3_evaluations: %" PRIu64 "\n", search_stats.mod3_evaluations);
    printf("move_lut_reads: %" PRIu64 "\n", search_stats.move_lut_reads);
    printf("heuristic_calls: %" PRIu64 "\n", search_stats.heuristic_calls);
#if VERIFY_OPTIMIZED
    printf("Move LUT element bytes: %zu\n",
           sizeof move_face + sizeof move_turn + sizeof next_face_start);
#else
    puts("Move LUT element bytes: 0");
#endif
    /* goal_tests counts a whole goal predicate, not its primitive comparisons.
     * div3/mod3 count only solve_ida expressions (including short-circuiting).
     * move_lut_reads excludes transition/distance tables and last_face loads.
     * heuristic_calls includes initial bound evaluation, but not H1 checks.
     * sizeof counts array element storage, not total linked static data.
     * Do not sum different metrics into a purported instruction count. */
    printf("Tables: %zu bytes; DFS frames: %zu bytes; path: %u bytes\n",
           sizeof permutation + sizeof orientation + sizeof permutation_distance + sizeof orientation_distance,
           sizeof(frame_t) * (MAX_SOLUTION + 1), MAX_SOLUTION);
    
    free(distance);
    return output_failed();
}