#include "sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char id[4];
    int  cpu_bound;
    int  arrival_time;
    int  num_bursts;
    int *cpu_bursts;
    int *io_bursts;
    int  burst_idx;
    int  remaining;
    int  ready_at;
    int  burst_rq_enter;
    int  cpu_start_time;
    long total_wait;
    long total_ta;
    int  num_cs;
    int  num_preempt;
    int  bursts_done;
    int  bursts_in_slice;
    double tau;
    int  run_gen;
} SimProc;

typedef enum {
    EV_CPU_START = 0,
    EV_ARRIVE    = 1,
    EV_IO_DONE   = 2,
    EV_RR_REQUEUE = 3,
    EV_CPU_DONE  = 4,
    EV_SLICE_EXP = 5
} EvType;

typedef struct { int time; EvType type; int pidx; int gen; } Event;

#define MAX_EV 600000
static Event    g_heap[MAX_EV];
static int      g_hsize;
static SimProc *g_sp;

static int ev_lt(const Event *a, const Event *b) {
    if (a->time != b->time) return a->time < b->time;
    if (a->type != b->type) return a->type < b->type;
    return strcmp(g_sp[a->pidx].id, g_sp[b->pidx].id) < 0;
}

static void ev_push(Event e) {
    int i = g_hsize++;
    g_heap[i] = e;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (ev_lt(&g_heap[i], &g_heap[p])) {
            Event t = g_heap[i]; g_heap[i] = g_heap[p]; g_heap[p] = t;
            i = p;
        } else break;
    }
}

static Event ev_pop(void) {
    Event ret = g_heap[0];
    g_heap[0] = g_heap[--g_hsize];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l < g_hsize && ev_lt(&g_heap[l], &g_heap[m])) m = l;
        if (r < g_hsize && ev_lt(&g_heap[r], &g_heap[m])) m = r;
        if (m == i) break;
        Event t = g_heap[i]; g_heap[i] = g_heap[m]; g_heap[m] = t;
        i = m;
    }
    return ret;
}

#define MAX_RQ 1000
static int g_rq[MAX_RQ];
static int g_rq_head, g_rq_tail;

static void rq_init(void) { g_rq_head = g_rq_tail = 0; }
static void rq_push_tail(int idx) { g_rq[g_rq_tail++] = idx; }
static int  rq_pop_head(void) { return g_rq[g_rq_head++]; }
static int  rq_empty(void) { return g_rq_head == g_rq_tail; }

static void rq_insert_sorted(int idx, int (*key_fn)(SimProc *, int, int), int opt) {
    int ins = g_rq_head;
    while (ins < g_rq_tail) {
        int j = g_rq[ins];
        int ka = key_fn(g_sp, idx, opt);
        int kb = key_fn(g_sp, j, opt);
        if (ka < kb || (ka == kb && strcmp(g_sp[idx].id, g_sp[j].id) < 0)) break;
        ins++;
    }
    memmove(&g_rq[ins + 1], &g_rq[ins], (size_t)(g_rq_tail - ins) * sizeof(g_rq[0]));
    g_rq[ins] = idx;
    g_rq_tail++;
}

static void print_rq(void) {
    if (g_rq_head == g_rq_tail) { printf("[Q: -]"); return; }
    printf("[Q:");
    for (int i = g_rq_head; i < g_rq_tail; i++)
        printf(" %s", g_sp[g_rq[i]].id);
    printf("]");
}

static int key_sjf(SimProc *sp, int i, int opt) {
    if (opt) {
        if (sp[i].remaining > 0) return sp[i].remaining;
        return sp[i].cpu_bursts[sp[i].burst_idx];
    }
    return (int)ceil(sp[i].tau - 1e-9);
}

static void collect_stats(SimProc *sp, int n, long cpu_busy, int sim_end,
                          SimStats *out, int has_rr) {
    long tw_cpu = 0, tw_io = 0, ta_cpu = 0, ta_io = 0;
    int  cs_cpu = 0, cs_io = 0, bd_cpu = 0, bd_io = 0;
    long bic_cpu = 0, bic_io = 0;
    int  pr_cpu = 0, pr_io = 0;

    for (int i = 0; i < n; i++) {
        if (sp[i].cpu_bound) {
            tw_cpu += sp[i].total_wait;
            ta_cpu += sp[i].total_ta;
            cs_cpu += sp[i].num_cs;
            bd_cpu += sp[i].bursts_done;
            bic_cpu += sp[i].bursts_in_slice;
            pr_cpu += sp[i].num_preempt;
        } else {
            tw_io += sp[i].total_wait;
            ta_io += sp[i].total_ta;
            cs_io += sp[i].num_cs;
            bd_io += sp[i].bursts_done;
            bic_io += sp[i].bursts_in_slice;
            pr_io += sp[i].num_preempt;
        }
    }

    out->cpu_util = (sim_end > 0) ? (double)cpu_busy / (double)sim_end * 100.0 : 0.0;
    out->avg_wait_cpu = bd_cpu ? (double)tw_cpu / bd_cpu : 0.0;
    out->avg_wait_io  = bd_io  ? (double)tw_io / bd_io : 0.0;
    out->avg_wait_all = (bd_cpu + bd_io) ? (double)(tw_cpu + tw_io) / (bd_cpu + bd_io) : 0.0;
    out->avg_ta_cpu   = bd_cpu ? (double)ta_cpu / bd_cpu : 0.0;
    out->avg_ta_io    = bd_io  ? (double)ta_io / bd_io : 0.0;
    out->avg_ta_all   = (bd_cpu + bd_io) ? (double)(ta_cpu + ta_io) / (bd_cpu + bd_io) : 0.0;
    out->cs_cpu = cs_cpu;
    out->cs_io  = cs_io;
    out->cs_all = cs_cpu + cs_io;
    out->preempt_cpu = pr_cpu;
    out->preempt_io  = pr_io;
    out->preempt_all = pr_cpu + pr_io;
    out->has_rr_stats = has_rr;
    if (has_rr) {
        out->pct_slice_cpu = bd_cpu ? (double)bic_cpu / bd_cpu * 100.0 : 0.0;
        out->pct_slice_io  = bd_io  ? (double)bic_io / bd_io * 100.0 : 0.0;
        out->pct_slice_all = (bd_cpu + bd_io)
            ? (double)(bic_cpu + bic_io) / (bd_cpu + bd_io) * 100.0 : 0.0;
    } else {
        out->pct_slice_cpu = out->pct_slice_io = out->pct_slice_all = 0.0;
    }
}

static const char *algo_name(AlgoType algo, int opt) {
    switch (algo) {
    case ALGO_FCFS: return "FCFS";
    case ALGO_SJF:  return opt ? "SJF-OPT" : "SJF";
    case ALGO_SRT:  return opt ? "SRT-OPT" : "SRT";
    case ALGO_RR:   return "RR";
    }
    return "?";
}

static void do_dispatch(int cur_time, int cpu_free_at, int half_cs, int *cpu_running) {
    if (rq_empty()) return;
    int next  = rq_pop_head();
    int start = (cpu_free_at > cur_time ? cpu_free_at : cur_time) + half_cs;
    *cpu_running = next;
    g_sp[next].run_gen++;
    ev_push((Event){ start, EV_CPU_START, next, g_sp[next].run_gen });
}

static void run_fcfs(SimProc *sp, int n, SimParams p, SimStats *out) {
    const int half = p.t_cs / 2;
    g_sp = sp;
    g_hsize = 0;
    rq_init();
    for (int i = 0; i < n; i++)
        ev_push((Event){ sp[i].arrival_time, EV_ARRIVE, i, 0 });

    int cpu_running = -1;
    int cpu_free_at = 0;
    int sim_end     = 0;
    long cpu_busy   = 0;

    printf("time 0ms: Simulator started for FCFS [Q: -]\n");

    while (g_hsize > 0) {
        Event e = ev_pop();
        int t  = e.time;
        int pi = e.pidx;
        SimProc *proc = &sp[pi];

        if (e.gen != 0 && e.gen != proc->run_gen)
            continue;

        switch (e.type) {
        case EV_ARRIVE:
            proc->ready_at = t;
            proc->burst_rq_enter = t;
            rq_push_tail(pi);
            printf("time %dms: Process %s arrived; added to ready queue ", t, proc->id);
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_CPU_START:
            proc->remaining      = proc->cpu_bursts[proc->burst_idx];
            proc->cpu_start_time = t;
            proc->total_wait += (long)(t - half - proc->ready_at);
            proc->num_cs++;
            printf("time %dms: Process %s started using the CPU for %dms burst ",
                   t, proc->id, proc->remaining);
            print_rq();
            printf("\n");
            ev_push((Event){ t + proc->remaining, EV_CPU_DONE, pi, proc->run_gen });
            break;

        case EV_CPU_DONE: {
            cpu_busy += (long)(t - proc->cpu_start_time);
            int bursts_left = proc->num_bursts - proc->burst_idx - 1;
            proc->total_ta += (long)(t + half - proc->burst_rq_enter);
            proc->bursts_done++;

            printf("time %dms: Process %s completed a CPU burst; %d burst%s to go ",
                   t, proc->id, bursts_left, bursts_left == 1 ? "" : "s");
            print_rq();
            printf("\n");

            if (bursts_left > 0) {
                int io_done = t + half + proc->io_bursts[proc->burst_idx];
                printf("time %dms: Process %s switching out of CPU; "
                       "blocking on I/O until time %dms ",
                       t, proc->id, io_done);
                print_rq();
                printf("\n");
                ev_push((Event){ io_done, EV_IO_DONE, pi, 0 });
            } else {
                printf("time %dms: Process %s terminated ", t, proc->id);
                print_rq();
                printf("\n");
                sim_end = t + half;
            }

            proc->burst_idx++;
            cpu_running = -1;
            cpu_free_at = t + half;
            if (!rq_empty())
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;
        }

        case EV_IO_DONE:
            proc->ready_at = t;
            proc->burst_rq_enter = t;
            rq_push_tail(pi);
            printf("time %dms: Process %s completed I/O; added to ready queue ",
                   t, proc->id);
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        default:
            break;
        }
    }

    collect_stats(sp, n, cpu_busy, sim_end, out, 0);
    printf("time %dms: Simulator ended for FCFS [Q: -]\n", sim_end);
}

static void run_sjf_like(SimProc *sp, int n, SimParams p, SimStats *out,
                         int preemptive, int opt) {
    const int half = p.t_cs / 2;
    g_sp = sp;
    g_hsize = 0;
    rq_init();

    for (int i = 0; i < n; i++)
        ev_push((Event){ sp[i].arrival_time, EV_ARRIVE, i, 0 });

    int cpu_running = -1;
    int cpu_free_at = 0;
    int sim_end     = 0;
    long cpu_busy   = 0;

    printf("time 0ms: Simulator started for %s [Q: -]\n",
           algo_name(preemptive ? ALGO_SRT : ALGO_SJF, opt));

    while (g_hsize > 0) {
        Event e = ev_pop();
        int t  = e.time;
        int pi = e.pidx;
        SimProc *proc = &sp[pi];

        if (e.gen != 0 && e.gen != proc->run_gen)
            continue;

        switch (e.type) {
        case EV_ARRIVE:
        case EV_IO_DONE: {
            proc->ready_at = t;
            proc->burst_rq_enter = t;

            if (preemptive && cpu_running != -1) {
                SimProc *runp = &sp[cpu_running];
                int run_key = opt ? runp->remaining : (int)ceil(runp->tau - 1e-9);
                int new_key = opt ? proc->cpu_bursts[proc->burst_idx]
                                  : (int)ceil(proc->tau - 1e-9);
                if (new_key < run_key ||
                    (new_key == run_key && strcmp(proc->id, runp->id) < 0)) {
                    int remaining = runp->remaining - (t - runp->cpu_start_time);
                    if (remaining < 0) remaining = 0;
                    runp->remaining = remaining;
                    runp->ready_at = t;
                    runp->num_preempt++;
                    cpu_busy += (long)(t - runp->cpu_start_time);
                    runp->run_gen++;
                    rq_insert_sorted(cpu_running, key_sjf, opt);
                    printf("time %dms: Process %s %s; preempting %s ", t, proc->id,
                           e.type == EV_ARRIVE ? "arrived" : "completed I/O",
                           runp->id);
                    print_rq();
                    printf("\n");
                    rq_insert_sorted(pi, key_sjf, opt);
                    cpu_running = -1;
                    cpu_free_at = t + p.t_cs;
                    if (!rq_empty())
                        do_dispatch(t, cpu_free_at - half, half, &cpu_running);
                    break;
                }
            }

            rq_insert_sorted(pi, key_sjf, opt);
            printf("time %dms: Process %s %s; added to ready queue ", t, proc->id,
                   e.type == EV_ARRIVE ? "arrived" : "completed I/O");
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;
        }

        case EV_CPU_START: {
            int burst_total = proc->cpu_bursts[proc->burst_idx];
            if (proc->remaining == 0)
                proc->remaining = burst_total;
            proc->cpu_start_time = t;
            if (proc->remaining == burst_total)
                proc->total_wait += (long)(t - half - proc->ready_at);
            proc->num_cs++;
            if (proc->remaining == burst_total)
                printf("time %dms: Process %s started using the CPU for %dms burst ",
                       t, proc->id, proc->remaining);
            else
                printf("time %dms: Process %s started using the CPU for remaining %dms of %dms burst ",
                       t, proc->id, proc->remaining, burst_total);
            print_rq();
            printf("\n");
            ev_push((Event){ t + proc->remaining, EV_CPU_DONE, pi, proc->run_gen });
            break;
        }

        case EV_CPU_DONE: {
            cpu_busy += (long)(t - proc->cpu_start_time);
            int actual = proc->cpu_bursts[proc->burst_idx];
            int bursts_left = proc->num_bursts - proc->burst_idx - 1;
            proc->total_ta += (long)(t + half - proc->burst_rq_enter);
            proc->bursts_done++;

            printf("time %dms: Process %s completed a CPU burst; %d burst%s to go ",
                   t, proc->id, bursts_left, bursts_left == 1 ? "" : "s");
            print_rq();
            printf("\n");

            if (!opt) {
                proc->tau = ceil(p.alpha * actual + (1.0 - p.alpha) * proc->tau - 1e-9);
                printf("time %dms: Recalculated tau for process %s to %dms ", t,
                       proc->id, (int)ceil(proc->tau - 1e-9));
                print_rq();
                printf("\n");
            }

            if (bursts_left > 0) {
                int io_done = t + half + proc->io_bursts[proc->burst_idx];
                printf("time %dms: Process %s switching out of CPU; blocking on I/O until time %dms ",
                       t, proc->id, io_done);
                print_rq();
                printf("\n");
                ev_push((Event){ io_done, EV_IO_DONE, pi, 0 });
            } else {
                printf("time %dms: Process %s terminated ", t, proc->id);
                print_rq();
                printf("\n");
                sim_end = t + half;
            }

            proc->burst_idx++;
            proc->remaining = 0;
            cpu_running = -1;
            cpu_free_at = t + half;
            if (!rq_empty())
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;
        }

        default:
            break;
        }
    }

    collect_stats(sp, n, cpu_busy, sim_end, out, 0);
    printf("time %dms: Simulator ended for %s [Q: -]\n",
           sim_end, algo_name(preemptive ? ALGO_SRT : ALGO_SJF, opt));
}

static void run_rr(SimProc *sp, int n, SimParams p, SimStats *out) {
    const int half = p.t_cs / 2;
    g_sp = sp;
    g_hsize = 0;
    rq_init();
    for (int i = 0; i < n; i++)
        ev_push((Event){ sp[i].arrival_time, EV_ARRIVE, i, 0 });

    int cpu_running = -1;
    int cpu_free_at = 0;
    int sim_end     = 0;
    long cpu_busy   = 0;

    printf("time 0ms: Simulator started for RR [Q: -]\n");

    while (g_hsize > 0) {
        Event e = ev_pop();
        int t  = e.time;
        int pi = e.pidx;
        SimProc *proc = &sp[pi];

        if (e.gen != 0 && e.gen != proc->run_gen)
            continue;

        switch (e.type) {
        case EV_ARRIVE:
            proc->ready_at = t;
            proc->burst_rq_enter = t;
            rq_push_tail(pi);
            printf("time %dms: Process %s arrived; added to ready queue ", t, proc->id);
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_IO_DONE:
            proc->ready_at = t;
            proc->burst_rq_enter = t;
            rq_push_tail(pi);
            printf("time %dms: Process %s completed I/O; added to ready queue ", t, proc->id);
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_RR_REQUEUE:
            proc->ready_at = t;
            rq_push_tail(pi);
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_CPU_START: {
            int burst_total = proc->cpu_bursts[proc->burst_idx];
            if (proc->remaining == 0)
                proc->remaining = burst_total;
            proc->cpu_start_time = t;
            proc->total_wait += (long)(t - half - proc->ready_at);
            proc->num_cs++;
            if (proc->remaining == burst_total)
                printf("time %dms: Process %s started using the CPU for %dms burst ",
                       t, proc->id, proc->remaining);
            else
                printf("time %dms: Process %s started using the CPU for remaining %dms of %dms burst ",
                       t, proc->id, proc->remaining, burst_total);
            print_rq();
            printf("\n");

            int run_for = proc->remaining < p.t_slice ? proc->remaining : p.t_slice;
            ev_push((Event){ t + proc->remaining, EV_CPU_DONE, pi, proc->run_gen });
            ev_push((Event){ t + run_for, EV_SLICE_EXP, pi, proc->run_gen });
            break;
        }

        case EV_SLICE_EXP:
            if (proc->remaining <= p.t_slice) {
                break;
            }
            if (rq_empty()) {
                proc->remaining -= p.t_slice;
                cpu_busy += p.t_slice;
                proc->cpu_start_time = t;
                proc->run_gen++;
                printf("time %dms: Time slice expired; no preemption because ready queue is empty ",
                       t);
                print_rq();
                printf("\n");
                ev_push((Event){ t + proc->remaining, EV_CPU_DONE, pi, proc->run_gen });
                ev_push((Event){ t + (proc->remaining < p.t_slice ? proc->remaining : p.t_slice),
                                 EV_SLICE_EXP, pi, proc->run_gen });
                break;
            }

            proc->remaining -= p.t_slice;
            proc->num_preempt++;
            cpu_busy += p.t_slice;
            proc->run_gen++;
            printf("time %dms: Time slice expired; preempting process %s with %dms remaining ",
                   t, proc->id, proc->remaining);
            print_rq();
            printf("\n");
            cpu_running = -1;
            cpu_free_at = t + half;
            ev_push((Event){ t + half, EV_RR_REQUEUE, pi, 0 });
            do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_CPU_DONE: {
            if (proc->cpu_bursts[proc->burst_idx] <= p.t_slice)
                proc->bursts_in_slice++;
            cpu_busy += proc->remaining;
            proc->remaining = 0;
            int bursts_left = proc->num_bursts - proc->burst_idx - 1;
            proc->total_ta += (long)(t + half - proc->burst_rq_enter);
            proc->bursts_done++;

            printf("time %dms: Process %s completed a CPU burst; %d burst%s to go ",
                   t, proc->id, bursts_left, bursts_left == 1 ? "" : "s");
            print_rq();
            printf("\n");

            if (bursts_left > 0) {
                int io_done = t + half + proc->io_bursts[proc->burst_idx];
                printf("time %dms: Process %s switching out of CPU; blocking on I/O until time %dms ",
                       t, proc->id, io_done);
                print_rq();
                printf("\n");
                ev_push((Event){ io_done, EV_IO_DONE, pi, 0 });
            } else {
                printf("time %dms: Process %s terminated ", t, proc->id);
                print_rq();
                printf("\n");
                sim_end = t + half;
            }

            proc->burst_idx++;
            cpu_running = -1;
            cpu_free_at = t + half;
            if (!rq_empty())
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;
        }
        }
    }

    collect_stats(sp, n, cpu_busy, sim_end, out, 1);
    printf("time %dms: Simulator ended for RR [Q: -]\n", sim_end);
}

void run_simulation(const Process *procs, int n, AlgoType algo,
                    SimParams params, SimStats *out) {
    SimProc *sp = calloc((size_t)n, sizeof(*sp));
    if (!sp) {
        fprintf(stderr, "ERROR: calloc failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n; i++) {
        strcpy(sp[i].id, procs[i].id);
        sp[i].cpu_bound    = procs[i].cpu_bound;
        sp[i].arrival_time = procs[i].arrival_time;
        sp[i].num_bursts   = procs[i].num_bursts;
        sp[i].cpu_bursts   = procs[i].cpu_bursts;
        sp[i].io_bursts    = procs[i].io_bursts;
        sp[i].tau          = (double)params.init_tau_ms;
    }

    int opt = (params.alpha == 0.0);
    switch (algo) {
    case ALGO_FCFS:
        run_fcfs(sp, n, params, out);
        break;
    case ALGO_SJF:
        run_sjf_like(sp, n, params, out, 0, opt);
        break;
    case ALGO_SRT:
        run_sjf_like(sp, n, params, out, 1, opt);
        break;
    case ALGO_RR:
        run_rr(sp, n, params, out);
        break;
    }

    free(sp);
}
