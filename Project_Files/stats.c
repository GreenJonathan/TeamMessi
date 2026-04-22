#include "stats.h"
#include <math.h>

static void burst_averages(FILE *fp, const Process *procs, int n, int ncpu)
{
    int nio = n - ncpu;
    long cpu_b_cpu = 0, cpu_b_io = 0, cpu_cnt_cpu = 0, cpu_cnt_io = 0;
    long io_b_cpu = 0, io_b_io = 0, io_cnt_cpu = 0, io_cnt_io = 0;

    for (int i = 0; i < n; i++) {
        const Process *p = &procs[i];
        int is_cpu = p->cpu_bound;
        for (int b = 0; b < p->num_bursts; b++) {
            if (is_cpu) {
                cpu_b_cpu += p->cpu_bursts[b];
                cpu_cnt_cpu++;
            } else {
                cpu_b_io += p->cpu_bursts[b];
                cpu_cnt_io++;
            }
        }
        for (int b = 0; b < p->num_bursts - 1; b++) {
            if (is_cpu) {
                io_b_cpu += p->io_bursts[b];
                io_cnt_cpu++;
            } else {
                io_b_io += p->io_bursts[b];
                io_cnt_io++;
            }
        }
    }

    double ac_cpu = cpu_cnt_cpu ? (double)cpu_b_cpu / cpu_cnt_cpu : 0.0;
    double ac_io  = cpu_cnt_io ? (double)cpu_b_io / cpu_cnt_io : 0.0;
    double ac_all = (cpu_cnt_cpu + cpu_cnt_io)
                        ? (double)(cpu_b_cpu + cpu_b_io) / (cpu_cnt_cpu + cpu_cnt_io)
                        : 0.0;
    double ai_cpu = io_cnt_cpu ? (double)io_b_cpu / io_cnt_cpu : 0.0;
    double ai_io  = io_cnt_io ? (double)io_b_io / io_cnt_io : 0.0;
    double ai_all = (io_cnt_cpu + io_cnt_io)
                        ? (double)(io_b_cpu + io_b_io) / (io_cnt_cpu + io_cnt_io)
                        : 0.0;

    fprintf(fp, "-- number of processes: %d\n", n);
    fprintf(fp, "-- number of CPU-bound processes: %d\n", ncpu);
    fprintf(fp, "-- number of I/O-bound processes: %d\n", nio);
    fprintf(fp, "-- CPU-bound average CPU burst time: %.2f ms\n", ac_cpu);
    fprintf(fp, "-- I/O-bound average CPU burst time: %.2f ms\n", ac_io);
    fprintf(fp, "-- overall average CPU burst time: %.2f ms\n", ac_all);
    fprintf(fp, "-- CPU-bound average I/O burst time: %.2f ms\n", ai_cpu);
    fprintf(fp, "-- I/O-bound average I/O burst time: %.2f ms\n", ai_io);
    fprintf(fp, "-- overall average I/O burst time: %.2f ms\n", ai_all);
}

static void one_algo(FILE *fp, const char *name, const SimStats *s)
{
    fprintf(fp, "\nAlgorithm %s\n", name);
    fprintf(fp, "-- CPU utilization: %.2f%%\n", s->cpu_util);
    fprintf(fp, "-- CPU-bound average wait time: %.2f ms\n", s->avg_wait_cpu);
    fprintf(fp, "-- I/O-bound average wait time: %.2f ms\n", s->avg_wait_io);
    fprintf(fp, "-- overall average wait time: %.2f ms\n", s->avg_wait_all);
    fprintf(fp, "-- CPU-bound average turnaround time: %.2f ms\n", s->avg_ta_cpu);
    fprintf(fp, "-- I/O-bound average turnaround time: %.2f ms\n", s->avg_ta_io);
    fprintf(fp, "-- overall average turnaround time: %.2f ms\n", s->avg_ta_all);
    fprintf(fp, "-- CPU-bound number of context switches: %d\n", s->cs_cpu);
    fprintf(fp, "-- I/O-bound number of context switches: %d\n", s->cs_io);
    fprintf(fp, "-- overall number of context switches: %d\n", s->cs_all);
    fprintf(fp, "-- CPU-bound number of preemptions: %d\n", s->preempt_cpu);
    fprintf(fp, "-- I/O-bound number of preemptions: %d\n", s->preempt_io);
    fprintf(fp, "-- overall number of preemptions: %d\n", s->preempt_all);
    if (s->has_rr_stats) {
        fprintf(fp, "-- CPU-bound percentage of CPU bursts completed within "
                    "one time slice: %.2f%%\n", s->pct_slice_cpu);
        fprintf(fp, "-- I/O-bound percentage of CPU bursts completed within "
                    "one time slice: %.2f%%\n", s->pct_slice_io);
        fprintf(fp, "-- overall percentage of CPU bursts completed within "
                    "one time slice: %.2f%%\n", s->pct_slice_all);
    }
}

void write_simout(FILE *fp, const Process *procs, int n, int ncpu,
                  const char *const algo_names[4], const SimStats stats[4])
{
    burst_averages(fp, procs, n, ncpu);
    for (int i = 0; i < 4; i++)
        one_algo(fp, algo_names[i], &stats[i]);
}
