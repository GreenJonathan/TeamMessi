#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "USAGE: %s <n> <ncpu> <seed> <lambda> <upper_bound>\n"
        "  n           -- total number of processes (>= 1)\n"
        "  ncpu        -- number of CPU-bound processes (0 <= ncpu <= n)\n"
        "  seed        -- seed for drand48()\n"
        "  lambda      -- rate parameter for exponential distribution (> 0)\n"
        "  upper_bound -- upper bound for exponential deviate (> 0)\n",
        prog);
}

static void print_processes(const Process *procs, int n, int ncpu)
{
    /* Header */
    printf("<<< PROJECT PART I -- process set (n=%d) with %d CPU-bound process%s >>>\n",
           n, ncpu, ncpu == 1 ? "" : "es");

    for (int i = 0; i < n; i++) {
        const Process *p = &procs[i];
        printf("%s-bound process %s: arrival time %dms; %d CPU burst%s:\n",
               p->cpu_bound ? "CPU" : "I/O",
               p->id,
               p->arrival_time,
               p->num_bursts,
               p->num_bursts == 1 ? "" : "s");

        for (int b = 0; b < p->num_bursts; b++) {
            if (b < p->num_bursts - 1) {
                printf("--> CPU burst %dms --> I/O burst %dms\n",
                       p->cpu_bursts[b], p->io_bursts[b]);
            } else {
                printf("--> CPU burst %dms\n", p->cpu_bursts[b]);
            }
        }
    }

}

int main(int argc, char *argv[])
{
    if (argc != 6) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    /* Parse arguments */
    char *end;

    long n_l = strtol(argv[1], &end, 10);
    if (*end != '\0' || n_l < 1) {
        fprintf(stderr, "ERROR: invalid n \"%s\"\n", argv[1]);
        return EXIT_FAILURE;
    }
    int n = (int)n_l;

    long ncpu_l = strtol(argv[2], &end, 10);
    if (*end != '\0' || ncpu_l < 0 || ncpu_l > n_l) {
        fprintf(stderr, "ERROR: invalid ncpu \"%s\"\n", argv[2]);
        return EXIT_FAILURE;
    }
    int ncpu = (int)ncpu_l;

    long seed_l = strtol(argv[3], &end, 10);
    if (*end != '\0') {
        fprintf(stderr, "ERROR: invalid seed \"%s\"\n", argv[3]);
        return EXIT_FAILURE;
    }
    long seed = seed_l;

    double lambda = strtod(argv[4], &end);
    if (*end != '\0' || lambda <= 0.0) {
        fprintf(stderr, "ERROR: invalid lambda \"%s\"\n", argv[4]);
        return EXIT_FAILURE;
    }

    long ub_l = strtol(argv[5], &end, 10);
    if (*end != '\0' || ub_l < 1) {
        fprintf(stderr, "ERROR: invalid upper_bound \"%s\"\n", argv[5]);
        return EXIT_FAILURE;
    }
    int upper_bound = (int)ub_l;

    /* Seed the RNG once */
    srand48(seed);

    /* Generate processes */
    Process *procs = generate_processes(n, ncpu, lambda, upper_bound);

    /* Print output */
    print_processes(procs, n, ncpu);

    /* Cleanup */
    free_processes(procs, n);

    return EXIT_SUCCESS;
}
