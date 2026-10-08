#define SEARCH_STATS
#define main solver_cli_main
#include "solver_opt.c"
#undef main

int main(int argc, char **argv)
{
    search_stats = (search_stats_t){0};

    int status = solver_cli_main(argc, argv);

    if (status == 0) {
        fprintf(stderr, "heuristic_calls = %llu\n",
                (unsigned long long)search_stats.heuristic_calls);
    }

    return status;
}
