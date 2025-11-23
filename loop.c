#include "egos.h"
#include <stdio.h>

int main(int argc, char** argv) {
    printf("Starting CPU loop (1 to 1 billion)...\n");

    /* 'volatile' tells the compiler: "Do not skip this logic!" */
    volatile unsigned long count = 0;
    unsigned long limit = 1000000000UL; // 1 Billion

    for (count = 0; count < limit; count++) {
        /* Burning CPU cycles */
    }

    printf("Finished counting to %lu.\n", limit);
    return 0;
}

