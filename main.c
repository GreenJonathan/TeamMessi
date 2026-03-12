#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"

static void print_processes(const Process *procs, int n, int ncpu,
                             long seed, double lambda, int upper_bound)
{
    /* Header lines */
    printf("<<< -- process set (n=%d) with %d CPU-bound process%s\n",
           n, ncpu, ncpu == 1 ? "" : "es");
    printf("<<< -- seed=%ld; lambda=%.6f; upper bound=%d\n",
           seed, lambda, upper_bound);

    for (int i = 0; i < n; i++) {
        const Process *p = &procs[i];

        /* Blank line before each process */
        printf("\n");

        printf("%s-bound process %s: arrival time %dms; %d CPU burst%s:\n",
               p->cpu_bound ? "CPU" : "I/O",
               p->id,
               p->arrival_time,
               p->num_bursts,
               p->num_bursts == 1 ? "" : "s");

        for (int b = 0; b < p->num_bursts; b++) {
            if (b < p->num_bursts - 1) {
                printf("==> CPU burst %dms ==> I/O burst %dms\n",
                       p->cpu_bursts[b], p->io_bursts[b]);
            } else {
                printf("==> CPU burst %dms\n", p->cpu_bursts[b]);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 6) {
        fprintf(stderr, "ERROR: Invalid number of arguments\n");
        return EXIT_FAILURE;
    }

    char *end;

    long n_l = strtol(argv[1], &end, 10);
    if (*end != '\0' || n_l < 1) {
        fprintf(stderr, "ERROR: Invalid number of processes\n");
        return EXIT_FAILURE;
    }
    int n = (int)n_l;

    long ncpu_l = strtol(argv[2], &end, 10);
    if (*end != '\0' || ncpu_l < 0 || ncpu_l > n_l) {
        fprintf(stderr, "ERROR: Invalid number of CPU-bound processes\n");
        return EXIT_FAILURE;
    }
    int ncpu = (int)ncpu_l;

    long seed_l = strtol(argv[3], &end, 10);
    if (*end != '\0') {
        fprintf(stderr, "ERROR: Invalid seed value\n");
        return EXIT_FAILURE;
    }
    long seed = seed_l;

    double lambda = strtod(argv[4], &end);
    if (*end != '\0' || lambda <= 0.0) {
        fprintf(stderr, "ERROR: Invalid lambda value\n");
        return EXIT_FAILURE;
    }

    long ub_l = strtol(argv[5], &end, 10);
    if (*end != '\0' || ub_l < 1) {
        fprintf(stderr, "ERROR: Invalid upper bound value\n");
        return EXIT_FAILURE;
    }
    int upper_bound = (int)ub_l;

    /* Seed the RNG once before generating processes */
    srand48(seed);

    /* Generate processes */
    Process *procs = generate_processes(n, ncpu, lambda, upper_bound);

    /* Print output */
    print_processes(procs, n, ncpu, seed, lambda, upper_bound);

    /* Cleanup */
    free_processes(procs, n);

    return EXIT_SUCCESS;
}