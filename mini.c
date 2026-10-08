#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { C = 7, O = 729, N = 3674160 };
typedef struct {
    unsigned char p[C], o[C];
} state_t;

static state_t turn(state_t s, int f)
{
    static const char map[][15] = {
        "14203561202100",
        "01245630001212",
        "02531460000000",
    };
    
    state_t t;
    for (int i = 0; i < C; ++i) {
        int j = map[f][i] - '0';
        t.p[i] = s.p[j];
        t.o[i] = (s.o[j] + map[f][i + C] - '0') % 3;
    }
    
    return t;
}

static size_t rank_state(state_t s)
{
    size_t p = 0, o = 0;
    
    for (int i = 0; i < C; ++i) {
        int n = 0;
        
        for (int j = i + 1; j < C; ++j)
            n += s.p[j] < s.p[i];
        
        p = p * (C - i) + n;
        if (i < 6)
            o = o * 3 + s.o[i];
    }
    
    return p * O + o;
}

int main(int argc, char **argv)
{
    state_t s;
    unsigned seen = 0, sum = 0;
    
    if (argc != 2 || strlen(argv[1]) != 14)
        return 2;

    for (int i = 0; i < C; ++i) {
        unsigned p = (unsigned) (argv[1][i] - '1');
        unsigned o = (unsigned) (argv[1][i + C] - '1');
        
        if (p >= C || o >= 3 || seen >> p & 1)
            return 2;
        s.p[i] = p;
        s.o[i] = o;
        seen |= 1U << p;
        sum += o;
    }
    
    if (sum % 3)
        return 2;

    unsigned char *step = calloc(N, 1);
    state_t *queue = malloc(sizeof(*queue) * N);
    size_t head = 0, tail = 1;
    
    if (!step || !queue)
        return free(step), free(queue), 1;
    
    queue[0] = (state_t) {{0, 1, 2, 3, 4, 5, 6}, {0}};
    while (head < tail) {
        state_t from = queue[head++];
        for (int f = 0; f < 3; ++f) {
            state_t to = from;
            for (int n = 0; n < 3; ++n) {
                size_t rank = rank_state(to = turn(to, f));
                if (rank && !step[rank]) {
                    step[rank] = f * 3 + 3 - n;
                    queue[tail++] = to;
                }
            }
        }
    }
    
    free(queue);
    if (tail != N)
        return free(step), 1;
    
    const char *sep = "";
    for (size_t rank; (rank = rank_state(s));) {
        int move = step[rank] - 1;
        printf(
            "%s%c%s", sep, "RBD" [move / 3],
            (const char *[]) { "", "2", "'" }[move % 3]);
        sep = " ";
        for (int n = move % 3 + 1; n--;)
            s = turn(s, move / 3);
    }

    /* Buffered stdout defers write errors to the flush. */
    int error = putchar('\n') < 0 || fflush(stdout) || ferror(stdout);
    free(step);
    
    return error;
}
