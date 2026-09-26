#include <stdint.h>

#ifndef REGION_BYTES
#define REGION_BYTES 4096
#endif

#ifndef PASSES
#define PASSES 1
#endif

static volatile uint32_t region[REGION_BYTES / sizeof(uint32_t)];

int main(void)
{
    for (uint32_t pass = 0; pass < PASSES; ++pass) {
        for (uint32_t i = 0; i < REGION_BYTES / sizeof(uint32_t); ++i) {
            region[i] = i + pass + 1;
        }
    }
    return 0;
}
