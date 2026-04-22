#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "process.h"
#include "sim.h"

static void print_processes(const Process *procs, int n, int ncpu,
                             long seed, double lambda, int upper_bound,
                             int t_cs, double alpha, int t_slice)
{
    printf("<<< -- process set (n=%d) with %d CPU-bound process%s\n",
           n, ncpu, ncpu == 1 ? "" : "es");
    printf("<<< -- seed=%ld; lambda=%.6f; upper bound=%d\n",
           seed, lambda, upper_bound);
    if (alpha <= 0.0)
        printf("<<< -- t_cs=%dms; alpha=<n/a>; t_slice=%dms\n", t_cs, t_slice);
    else
        printf("<<< -- t_cs=%dms; alpha=%.2f; t_slice=%dms\n", t_cs, alpha, t_slice);

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

static void print_procset_stats(FILE *f, const Process *procs, int n, int ncpu)
{
    int nio = n - ncpu;

    /* sum CPU burst times */
    double sum_cpu_cpu = 0.0, sum_io_cpu = 0.0;
    int    cnt_cpu_cpu = 0,   cnt_io_cpu = 0;
    double sum_cpu_io  = 0.0, sum_io_io  = 0.0;
    int    cnt_cpu_io  = 0,   cnt_io_io  = 0;

    for (int i = 0; i < n; i++) {
        const Process *p = &procs[i];
        for (int b = 0; b < p->num_bursts; b++) {
            if (p->cpu_bound) { sum_cpu_cpu += p->cpu_bursts[b]; cnt_cpu_cpu++; }
            else              { sum_io_cpu  += p->cpu_bursts[b]; cnt_io_cpu++;  }
        }
        for (int b = 0; b < p->num_bursts - 1; b++) {
            if (p->cpu_bound) { sum_cpu_io += p->io_bursts[b]; cnt_cpu_io++; }
            else              { sum_io_io  += p->io_bursts[b]; cnt_io_io++;  }
        }
    }

    fprintf(f, "-- number of processes: %d\n", n);
    fprintf(f, "-- number of CPU-bound processes: %d\n", ncpu);
    fprintf(f, "-- number of I/O-bound processes: %d\n", nio);

    fprintf(f, "-- CPU-bound average CPU burst time: %.2f ms\n",
            cnt_cpu_cpu ? sum_cpu_cpu / cnt_cpu_cpu : 0.0);
    fprintf(f, "-- I/O-bound average CPU burst time: %.2f ms\n",
            cnt_io_cpu  ? sum_io_cpu  / cnt_io_cpu  : 0.0);
    fprintf(f, "-- overall average CPU burst time: %.2f ms\n",
            (cnt_cpu_cpu+cnt_io_cpu) ? (sum_cpu_cpu+sum_io_cpu)/(cnt_cpu_cpu+cnt_io_cpu) : 0.0);

    fprintf(f, "-- CPU-bound average I/O burst time: %.2f ms\n",
            cnt_cpu_io  ? sum_cpu_io  / cnt_cpu_io  : 0.0);
    fprintf(f, "-- I/O-bound average I/O burst time: %.2f ms\n",
            cnt_io_io   ? sum_io_io   / cnt_io_io   : 0.0);
    fprintf(f, "-- overall average I/O burst time: %.2f ms\n",
            (cnt_cpu_io+cnt_io_io) ? (sum_cpu_io+sum_io_io)/(cnt_cpu_io+cnt_io_io) : 0.0);
}

static void print_algo_stats(FILE *f, const char *name, const SimStats *s)
{
    fprintf(f, "\nAlgorithm %s\n", name);
    fprintf(f, "-- CPU utilization: %.2f%%\n",           s->cpu_util);
    fprintf(f, "-- CPU-bound average wait time: %.2f ms\n", s->avg_wait_cpu);
    fprintf(f, "-- I/O-bound average wait time: %.2f ms\n", s->avg_wait_io);
    fprintf(f, "-- overall average wait time: %.2f ms\n",   s->avg_wait_all);
    fprintf(f, "-- CPU-bound average turnaround time: %.2f ms\n", s->avg_ta_cpu);
    fprintf(f, "-- I/O-bound average turnaround time: %.2f ms\n", s->avg_ta_io);
    fprintf(f, "-- overall average turnaround time: %.2f ms\n",   s->avg_ta_all);
    fprintf(f, "-- CPU-bound number of context switches: %d\n", s->cs_cpu);
    fprintf(f, "-- I/O-bound number of context switches: %d\n", s->cs_io);
    fprintf(f, "-- overall number of context switches: %d\n",   s->cs_all);
    fprintf(f, "-- CPU-bound number of preemptions: %d\n", s->preempt_cpu);
    fprintf(f, "-- I/O-bound number of preemptions: %d\n", s->preempt_io);
    fprintf(f, "-- overall number of preemptions: %d\n",   s->preempt_all);
    if (s->has_rr_stats) {
        fprintf(f, "-- CPU-bound percentage of CPU bursts completed within one time slice: %.2f%%\n",
                s->pct_slice_cpu);
        fprintf(f, "-- I/O-bound percentage of CPU bursts completed within one time slice: %.2f%%\n",
                s->pct_slice_io);
        fprintf(f, "-- overall percentage of CPU bursts completed within one time slice: %.2f%%\n",
                s->pct_slice_all);
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
    if (*end != '\0' || tcs_l < 2 || tcs_l % 2 != 0) {
        fprintf(stderr, "ERROR: Invalid t_cs value\n");
        return EXIT_FAILURE;
    }
    int t_cs = (int)tcs_l;

    double alpha = strtod(argv[7], &end);
    if (*end != '\0') {
        fprintf(stderr, "ERROR: Invalid alpha value\n");
        return EXIT_FAILURE;
    }

    long ts_l = strtol(argv[8], &end, 10);
    if (*end != '\0' || ts_l < 1) {
        fprintf(stderr, "ERROR: Invalid t_slice value\n");
        return EXIT_FAILURE;
    }
    int t_slice = (int)ts_l;

    srand48(seed);

    Process *procs = generate_processes(n, ncpu, lambda, upper_bound);

    print_processes(procs, n, ncpu, seed, lambda, upper_bound,
                    t_cs, alpha, t_slice);

    /* open simout file for statistics */
    FILE *simout = fopen("simout.txt", "w");
    if (!simout) {
        fprintf(stderr, "ERROR: Cannot open simout.txt\n");
        free_processes(procs, n);
        return EXIT_FAILURE;
    }

    print_procset_stats(simout, procs, n, ncpu);

    SimParams params = { t_cs, alpha, lambda, t_slice };
    int opt = (alpha <= 0.0);

    printf("\n<<< PROJECT SIMULATIONS\n\n");

    SimStats stats;

    /* FCFS */
    run_simulation(procs, n, ALGO_FCFS, params, &stats);
    print_algo_stats(simout, "FCFS", &stats);
    printf("\n");

    /* SJF */
    run_simulation(procs, n, ALGO_SJF, params, &stats);
    print_algo_stats(simout, opt ? "SJF-OPT" : "SJF", &stats);
    printf("\n");

    /* SRT */
    run_simulation(procs, n, ALGO_SRT, params, &stats);
    print_algo_stats(simout, opt ? "SRT-OPT" : "SRT", &stats);
    printf("\n");

    /* RR */
    run_simulation(procs, n, ALGO_RR, params, &stats);
    print_algo_stats(simout, "RR", &stats);

    fclose(simout);
    free_processes(procs, n);

    return EXIT_SUCCESS;
}
