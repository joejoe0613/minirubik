/* export_tables.c -- gcc -O2 -o export_tables export_tables.c && ./export_tables > rubik_tables.s */
#define main solver_main          /* 借用 solver_opt.c 的函式，避開它的 main */
#include "solver_opt.c"
#undef main

static void dump_half(const char *label, const uint16_t *v, unsigned n)
{
    printf(".balign 4\n%s:\n", label);
    for (unsigned i = 0; i < n; ++i)
        printf("%s%u%s", i % 16 == 0 ? "    .half " : ", ", v[i],
               i % 16 == 15 || i + 1 == n ? "\n" : "");
}

static void dump_byte(const char *label, const uint8_t *v, unsigned n)
{
    printf(".balign 4\n%s:\n", label);
    for (unsigned i = 0; i < n; ++i)
        printf("%s%u%s", i % 32 == 0 ? "    .byte " : ", ", v[i],
               i % 32 == 31 || i + 1 == n ? "\n" : "");
}

int main(void)
{
    if (!build_tables())
        return 1;
    puts(".data");
    dump_half("perm_table",   &permutation[0][0], 3 * PERMUTATIONS);
    dump_half("orient_table", &orientation[0][0], 3 * ORIENTATIONS);
    dump_byte("perm_dist",    permutation_distance, PERMUTATIONS);
    dump_byte("orient_dist",  orientation_distance, ORIENTATIONS);
    return 0;
}
