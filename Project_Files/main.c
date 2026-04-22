#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "process.h"
#include "sim.h"
#include "stats.h"

static void print_processes(const Process *procs, int n, int ncpu,
                            long seed, double lambda, int upper_bound,
                            int t_cs, int opt_mode, double alpha, int t_slice)
{
    printf("<<< -- process set (n=%d) with %d CPU-bound process%s\n",
           n, ncpu, ncpu == 1 ? "" : "es");
    printf("<<< -- seed=%ld; lambda=%.6f; upper bound=%d\n",
           seed, lambda, upper_bound);
    if (opt_mode)
        printf("<<< -- t_cs=%dms; alpha=<n/a>; t_slice=%dms\n", t_cs, t_slice);
    else
        printf("<<< -- t_cs=%dms; alpha=%.6f; t_slice=%dms\n", t_cs, alpha, t_slice);

    printf("\n");
    for (int i = 0; i < n; i++) {
        const Process *p = &procs[i];
        printf("%s-bound process %s: arrival time %dms; %d CPU burst%s\n",
               p->cpu_bound ? "CPU" : "I/O",
               p->id,
               p->arrival_time,
               p->num_bursts,
               p->num_bursts == 1 ? "" : "s");
    }
}

int main(int argc, char *argv[])
{
    if (argc != 9) {
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

    long tcs_l = strtol(argv[6], &end, 10);
    if (*end != '\0' || tcs_l < 0) {
        fprintf(stderr, "ERROR: Invalid context switch time\n");
        return EXIT_FAILURE;
    }
    int t_cs = (int)tcs_l;

    int    opt_mode = 0;
    double alpha    = 0.0;
    if (strcasecmp(argv[7], "n/a") == 0 || strcmp(argv[7], "-1") == 0) {
        opt_mode = 1;
        alpha    = 0.0;
    } else {
        alpha = strtod(argv[7], &end);
        if (*end != '\0' || alpha < 0.0 || alpha > 1.0) {
            fprintf(stderr, "ERROR: Invalid alpha value\n");
            return EXIT_FAILURE;
        }
    }

    long slice_l = strtol(argv[8], &end, 10);
    if (*end != '\0' || slice_l < 1) {
        fprintf(stderr, "ERROR: Invalid time slice value\n");
        return EXIT_FAILURE;
    }
    int t_slice = (int)slice_l;

    srand48(seed);
    Process *procs = generate_processes(n, ncpu, lambda, upper_bound);

    print_processes(procs, n, ncpu, seed, lambda, upper_bound, t_cs, opt_mode, alpha, t_slice);

    SimParams params = {
        .t_cs         = t_cs,
        .alpha        = alpha,
        .t_slice      = t_slice,
        .init_tau_ms  = (int)ceil(1.0 / lambda),
    };

    const char *names[4] = {
        "FCFS",
        opt_mode ? "SJF-OPT" : "SJF",
        opt_mode ? "SRT-OPT" : "SRT",
        "RR"
    };

    SimStats stats[4];
    AlgoType order[4] = { ALGO_FCFS, ALGO_SJF, ALGO_SRT, ALGO_RR };

    printf("\n<<< PROJECT SIMULATIONS\n\n");
    for (int i = 0; i < 4; i++) {
        run_simulation(procs, n, order[i], params, &stats[i]);
        printf("\n");
    }

    FILE *fp = fopen("simout.txt", "w");
    if (!fp) {
        fprintf(stderr, "ERROR: could not open simout.txt for writing\n");
        free_processes(procs, n);
        return EXIT_FAILURE;
    }
    write_simout(fp, procs, n, ncpu, names, stats);
    fclose(fp);

    free_processes(procs, n);
    return EXIT_SUCCESS;
}
