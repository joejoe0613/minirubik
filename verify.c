/* Host-only validation. Default: EVERY rank. Optional count: spread sample. */
#include <time.h>
#define main solver_cli_main
#include "solver.c"
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
    
    uint8_t *distance = oracle_distances();
    
    if (!distance || !build_tables()) return 1;

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

    /* ==============================================================
     * 4. IDA* 抽樣 / 全狀態驗證迴圈 (驗證下界可採納性與求解最優性)
     * ============================================================== */
    clock_t start = clock();
    
    for (unsigned i = 0; i < count; ++i) {
        uint32_t r = (uint32_t) ((uint64_t) i * STATES / count);
        uint16_t p = (uint16_t) (r / ORIENTATIONS), o = (uint16_t) (r % ORIENTATIONS);
        uint8_t path[MAX_SOLUTION];
        int n = solve_ida(p, o, path);
        
        if (lower_bound(p, o) > distance[r] || n != distance[r] ||
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
    
    printf("PASS %u/%u states: admissible h, optimal length, original-model replay; %.3f CPU s\n",
           count, STATES, (double) (clock() - start) / CLOCKS_PER_SEC);
    printf("Tables: %zu bytes; DFS frames: %zu bytes; path: %u bytes\n",
           sizeof permutation + sizeof orientation + sizeof permutation_distance + sizeof orientation_distance,
           sizeof(frame_t) * (MAX_SOLUTION + 1), MAX_SOLUTION);
    
    free(distance);
    return output_failed();
}