#ifndef SIM_H
#define SIM_H

#include "process.h"

typedef enum { ALGO_FCFS, ALGO_SJF, ALGO_SRT, ALGO_RR } AlgoType;

typedef struct {
    int    t_cs;
    double alpha;
    int    t_slice;
    int    init_tau_ms; /* ceil(1/lambda), used for SJF/SRT tau seed */
} SimParams;

typedef struct {
    double cpu_util;
    double avg_wait_cpu,  avg_wait_io,  avg_wait_all;
    double avg_ta_cpu,    avg_ta_io,    avg_ta_all;
    int    cs_cpu,        cs_io,        cs_all;
    int    preempt_cpu,   preempt_io,   preempt_all;
    double pct_slice_cpu, pct_slice_io, pct_slice_all;
    int    has_rr_stats;
} SimStats;

void run_simulation(const Process *procs, int n, AlgoType algo,
                    SimParams params, SimStats *out);

#endif /* SIM_H */
