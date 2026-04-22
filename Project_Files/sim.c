#include "sim.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── per-process simulation state ─────────────────────────────────────── */
typedef struct {
    char id[4];
    int  cpu_bound;
    int  arrival_time;
    int  num_bursts;
    int *cpu_bursts;
    int *io_bursts;
    /* sim state */
    int  burst_idx;       /* index of the burst currently executing / next */
    int  remaining;       /* remaining ms in current CPU burst              */
    int  ready_at;        /* time entered ready queue for this burst        */
    int  cpu_start_time;  /* time this burst started on CPU                 */
    /* per-process stats */
    long total_wait;
    long total_ta;
    int  num_cs;
    int  num_preempt;
    int  bursts_done;
    int  bursts_in_slice; /* RR: bursts completed without slice expiry      */
    double tau;           /* SJF/SRT estimated CPU burst length             */
    int  event_token;     /* invalidates stale CPU events after preemption  */
} SimProc;

/* ── event queue (min-heap) ──────────────────────────────────────────── */

/* Lower ordinal = higher priority when times are equal */
typedef enum {
    EV_CPU_START = 0,   /* process begins using CPU                        */
    EV_ARRIVE    = 1,   /* process arrives for the first time              */
    EV_IO_DONE   = 2,   /* process finishes I/O, re-enters ready queue     */
    EV_CPU_DONE  = 3,   /* CPU burst completes                             */
    EV_SLICE_EXP = 4    /* RR time-slice expires                           */
} EvType;

typedef struct { int time; EvType type; int pidx; int token; } Event;

#define MAX_EV 600000
static Event   g_heap[MAX_EV];
static int     g_hsize;
static SimProc *g_sp;   /* set before each simulation run */

static int ev_lt(const Event *a, const Event *b) {
    if (a->time != b->time) return a->time < b->time;
    if (a->type != b->type) re
    turn a->type < b->type;
    return strcmp(g_sp[a->pidx].id, g_sp[b->pidx].id) < 0;
}

static void ev_push(Event e) {
    int i = g_hsize++;
    g_heap[i] = e;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (ev_lt(&g_heap[i], &g_heap[p])) {
            Event tmp = g_heap[i]; g_heap[i] = g_heap[p]; g_heap[p] = tmp;
            i = p;
        } else break;
    }
}

static Event ev_pop(void) {
    Event ret = g_heap[0];
    g_heap[0] = g_heap[--g_hsize];
    int i = 0;
    for (;;) {
        int l = 2*i+1, r = 2*i+2, m = i;
        if (l < g_hsize && ev_lt(&g_heap[l], &g_heap[m])) m = l;
        if (r < g_hsize && ev_lt(&g_heap[r], &g_heap[m])) m = r;
        if (m == i) break;
        Event tmp = g_heap[i]; g_heap[i] = g_heap[m]; g_heap[m] = tmp;
        i = m;
    }
    return ret;
}

/* ── FIFO ready queue ────────────────────────────────────────────────── */
#define MAX_RQ 1000
static int g_rq[MAX_RQ];
static int g_rq_head, g_rq_tail;

static void rq_init(void)      { g_rq_head = g_rq_tail = 0; }
static void rq_push(int idx)   { g_rq[g_rq_tail++] = idx; }
static int  rq_pop(void)       { return g_rq[g_rq_head++]; }
static int  rq_empty(void)     { return g_rq_head == g_rq_tail; }

static int rq_remove(int idx) {
    for (int i = g_rq_head; i < g_rq_tail; i++) {
        if (g_rq[i] == idx) {
            for (int j = i; j < g_rq_tail - 1; j++)
                g_rq[j] = g_rq[j + 1];
            g_rq_tail--;
            return 1;
        }
    }
    return 0;
}

static void print_rq(void) {
    if (g_rq_head == g_rq_tail) { printf("[Q: -]"); return; }
    printf("[Q:");
    for (int i = g_rq_head; i < g_rq_tail; i++)
        printf(" %s", g_sp[g_rq[i]].id);
    printf("]");
}

/* Dispatch the front of the ready queue.
 * cpu_free_at: the earliest time the CPU is free to accept a switch-in.
 * Sets *cpu_running to the dispatched process index and schedules EV_CPU_START.
 */
static void do_dispatch(int cur_time, int cpu_free_at, int half_cs,
                         int *cpu_running) {
    if (rq_empty()) return;
    int next   = rq_pop();
    int start  = (cpu_free_at > cur_time ? cpu_free_at : cur_time) + half_cs;
    *cpu_running = next;
    Event se = { start, EV_CPU_START, next, g_sp[next].event_token };
    ev_push(se);
}

/* ── algorithm name helper ───────────────────────────────────────────── */
static const char *algo_name(AlgoType algo, int opt) {
    switch (algo) {
        case ALGO_FCFS: return "FCFS";
        case ALGO_SJF:  return opt ? "SJF-OPT" : "SJF";
        case ALGO_SRT:  return opt ? "SRT-OPT" : "SRT";
        case ALGO_RR:   return "RR";
    }
    return "?";
}

static double sched_key(const SimProc *proc, int opt, int use_remaining) {
    if (opt) {
        if (use_remaining && proc->remaining > 0)
            return (double)proc->remaining;
        return (double)proc->cpu_bursts[proc->burst_idx];
    }
    return proc->tau;
}

static int sjf_lt(int a, int b, int opt, int use_remaining) {
    double ka = sched_key(&g_sp[a], opt, use_remaining);
    double kb = sched_key(&g_sp[b], opt, use_remaining);
    if (ka != kb) return ka < kb;
    return strcmp(g_sp[a].id, g_sp[b].id) < 0;
}

static void rq_push_sorted(int idx, int opt, int use_remaining) {
    int pos = g_rq_tail;
    while (pos > g_rq_head && sjf_lt(idx, g_rq[pos - 1], opt, use_remaining)) {
        g_rq[pos] = g_rq[pos - 1];
        pos--;
    }
    g_rq[pos] = idx;
    g_rq_tail++;
}

static void collect_stats(SimProc *sp, int n, int sim_end, long cpu_busy,
                          SimStats *out) {
    long tw_cpu=0, tw_io=0;
    long ta_cpu=0, ta_io=0;
    int  cs_cpu=0, cs_io=0;
    int  bd_cpu=0, bd_io=0;
    int  pr_cpu=0, pr_io=0;

    for (int i = 0; i < n; i++) {
        if (sp[i].cpu_bound) {
            tw_cpu += sp[i].total_wait;
            ta_cpu += sp[i].total_ta;
            cs_cpu += sp[i].num_cs;
            bd_cpu += sp[i].bursts_done;
            pr_cpu += sp[i].num_preempt;
        } else {
            tw_io  += sp[i].total_wait;
            ta_io  += sp[i].total_ta;
            cs_io  += sp[i].num_cs;
            bd_io  += sp[i].bursts_done;
            pr_io  += sp[i].num_preempt;
        }
    }

    out->cpu_util    = (sim_end > 0) ? (double)cpu_busy / sim_end * 100.0 : 0.0;
    out->avg_wait_cpu = bd_cpu ? (double)tw_cpu / bd_cpu : 0.0;
    out->avg_wait_io  = bd_io  ? (double)tw_io  / bd_io  : 0.0;
    out->avg_wait_all = (bd_cpu+bd_io) ? (double)(tw_cpu+tw_io)/(bd_cpu+bd_io) : 0.0;
    out->avg_ta_cpu  = bd_cpu ? (double)ta_cpu / bd_cpu : 0.0;
    out->avg_ta_io   = bd_io  ? (double)ta_io  / bd_io  : 0.0;
    out->avg_ta_all  = (bd_cpu+bd_io) ? (double)(ta_cpu+ta_io)/(bd_cpu+bd_io) : 0.0;
    out->cs_cpu      = cs_cpu;
    out->cs_io       = cs_io;
    out->cs_all      = cs_cpu + cs_io;
    out->preempt_cpu = pr_cpu;
    out->preempt_io  = pr_io;
    out->preempt_all = pr_cpu + pr_io;
    out->has_rr_stats = 0;
}

/* ── FCFS simulation ─────────────────────────────────────────────────── */
#define PRINT_LIMIT 10000

static void run_fcfs(SimProc *sp, int n, SimParams p, SimStats *out) {
    const int half = p.t_cs / 2;
    g_sp = sp;

    g_hsize = 0;
    rq_init();
    for (int i = 0; i < n; i++) {
        Event e = { sp[i].arrival_time, EV_ARRIVE, i, 0 };
        ev_push(e);
    }

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

        switch (e.type) {

        case EV_ARRIVE:
            proc->ready_at = t;
            rq_push(pi);
            if (t <= PRINT_LIMIT) {
                printf("time %dms: Process %s arrived; added to ready queue ", t, proc->id);
                print_rq();
                printf("\n");
            }
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_CPU_START:
            proc->remaining      = proc->cpu_bursts[proc->burst_idx];
            proc->cpu_start_time = t;
            proc->total_wait    += (long)(t - half - proc->ready_at);
            proc->num_cs++;
            if (t <= PRINT_LIMIT) {
                printf("time %dms: Process %s started using the CPU for %dms burst ",
                       t, proc->id, proc->remaining);
                print_rq();
                printf("\n");
            }
            ev_push((Event){ t + proc->remaining, EV_CPU_DONE, pi, 0 });
            break;

        case EV_CPU_DONE: {
            cpu_busy += (long)(t - proc->cpu_start_time);
            int bursts_left = proc->num_bursts - proc->burst_idx - 1;
            proc->total_ta += (long)(t + half - proc->ready_at);
            proc->bursts_done++;

            if (bursts_left > 0) {
                int io_done = t + half + proc->io_bursts[proc->burst_idx];
                if (t <= PRINT_LIMIT) {
                    printf("time %dms: Process %s completed a CPU burst; %d burst%s to go ",
                           t, proc->id, bursts_left, bursts_left == 1 ? "" : "s");
                    print_rq();
                    printf("\n");
                    printf("time %dms: Process %s switching out of CPU; "
                           "blocking on I/O until time %dms ",
                           t, proc->id, io_done);
                    print_rq();
                    printf("\n");
                }
                ev_push((Event){ io_done, EV_IO_DONE, pi, 0 });
            } else {
                if (t <= PRINT_LIMIT) {
                    printf("time %dms: Process %s terminated ", t, proc->id);
                    print_rq();
                    printf("\n");
                }
                sim_end = t + half;
            }

            proc->burst_idx++;
            cpu_free_at  = t + half;
            cpu_running  = -1;

            if (!rq_empty())
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;
        }

        case EV_IO_DONE:
            proc->ready_at = t;
            rq_push(pi);
            if (t <= PRINT_LIMIT) {
                printf("time %dms: Process %s completed I/O; added to ready queue ",
                       t, proc->id);
                print_rq();
                printf("\n");
            }
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_SLICE_EXP:
            break;
        }
    }

    printf("time %dms: Simulator ended for FCFS [Q: -]\n", sim_end);

    /* ── collect stats ── */
    long tw_cpu=0, tw_io=0;
    long ta_cpu=0, ta_io=0;
    int  cs_cpu=0, cs_io=0;
    int  bd_cpu=0, bd_io=0;

    for (int i = 0; i < n; i++) {
        if (sp[i].cpu_bound) {
            tw_cpu += sp[i].total_wait;
            ta_cpu += sp[i].total_ta;
            cs_cpu += sp[i].num_cs;
            bd_cpu += sp[i].bursts_done;
        } else {
            tw_io  += sp[i].total_wait;
            ta_io  += sp[i].total_ta;
            cs_io  += sp[i].num_cs;
            bd_io  += sp[i].bursts_done;
        }
    }

    out->cpu_util    = (sim_end > 0) ? (double)cpu_busy / sim_end * 100.0 : 0.0;
    out->avg_wait_cpu = bd_cpu ? (double)tw_cpu / bd_cpu : 0.0;
    out->avg_wait_io  = bd_io  ? (double)tw_io  / bd_io  : 0.0;
    out->avg_wait_all = (bd_cpu+bd_io) ? (double)(tw_cpu+tw_io)/(bd_cpu+bd_io) : 0.0;
    out->avg_ta_cpu  = bd_cpu ? (double)ta_cpu / bd_cpu : 0.0;
    out->avg_ta_io   = bd_io  ? (double)ta_io  / bd_io  : 0.0;
    out->avg_ta_all  = (bd_cpu+bd_io) ? (double)(ta_cpu+ta_io)/(bd_cpu+bd_io) : 0.0;
    out->cs_cpu      = cs_cpu;
    out->cs_io       = cs_io;
    out->cs_all      = cs_cpu + cs_io;
    out->preempt_cpu = out->preempt_io = out->preempt_all = 0;
    out->has_rr_stats = 0;
}

/* ── stub for unimplemented algorithms ──────────────────────────────── */
static int srt_should_preempt(int t, int pi, int running, int opt) {
    int elapsed, rem;
    if (running == -1)
        return 0;
    if (g_sp[running].cpu_start_time == 0)
        return 0;

    elapsed = t - g_sp[running].cpu_start_time;
    rem = g_sp[running].remaining - elapsed;
    if (rem < 0) rem = 0;
    return sched_key(&g_sp[pi], opt, 0) < (double)rem;
}

static void srt_preempt(int t, int pi, const char *source, int half, int opt,
                        int *cpu_running, int *cpu_free_at, long *cpu_busy) {
    int running = *cpu_running;
    SimProc *cur = &g_sp[running];
    int elapsed = cur->cpu_start_time ? t - cur->cpu_start_time : 0;
    int rem = cur->remaining - elapsed;
    if (rem < 0) rem = 0;

    rq_remove(pi);
    *cpu_busy += elapsed;
    cur->remaining = rem;
    cur->num_preempt++;
    cur->event_token++;
    cur->cpu_start_time = 0;
    cur->ready_at = t;

    printf("time %dms: Process %s %s; preempting %s (remaining time %dms) ",
           t, g_sp[pi].id, source, cur->id, rem);
    print_rq();
    printf("\n");

    rq_push_sorted(running, opt, 1);
    rq_push_sorted(pi, opt, 1);
    *cpu_running = -1;
    *cpu_free_at = t + half;
    do_dispatch(t, *cpu_free_at, half, cpu_running);
}

static void run_sjf_srt(SimProc *sp, int n, AlgoType algo, SimParams p,
                        SimStats *out) {
    const int half = p.t_cs / 2;
    int opt = (p.alpha <= 0.0);
    int preemptive = (algo == ALGO_SRT);
    const char *name = algo_name(algo, opt);
    double initial_tau = (p.lambda > 0.0) ? ceil(1.0 / p.lambda) : 1000.0;
    g_sp = sp;

    g_hsize = 0;
    rq_init();
    for (int i = 0; i < n; i++) {
        sp[i].tau = initial_tau;
        Event e = { sp[i].arrival_time, EV_ARRIVE, i, 0 };
        ev_push(e);
    }

    int cpu_running = -1;
    int cpu_free_at = 0;
    int sim_end = 0;
    long cpu_busy = 0;

    printf("time 0ms: Simulator started for %s [Q: -]\n", name);

    while (g_hsize > 0) {
        Event e = ev_pop();
        int t = e.time;
        int pi = e.pidx;
        SimProc *proc = &sp[pi];

        if ((e.type == EV_CPU_START || e.type == EV_CPU_DONE) &&
            e.token != proc->event_token)
            continue;

        switch (e.type) {
        case EV_ARRIVE:
            proc->ready_at = t;
            proc->remaining = proc->cpu_bursts[proc->burst_idx];
            rq_push_sorted(pi, opt, preemptive);
            if (preemptive && srt_should_preempt(t, pi, cpu_running, opt)) {
                srt_preempt(t, pi, "arrived", half, opt, &cpu_running,
                            &cpu_free_at, &cpu_busy);
            } else {
                if (t <= PRINT_LIMIT) {
                    printf("time %dms: Process %s arrived; added to ready queue ",
                           t, proc->id);
                    print_rq();
                    printf("\n");
                }
            }
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_CPU_START: {
            int total = proc->cpu_bursts[proc->burst_idx];
            proc->cpu_start_time = t;
            proc->total_wait += (long)(t - half - proc->ready_at);
            proc->num_cs++;
            if (t <= PRINT_LIMIT) {
                if (proc->remaining == total) {
                    printf("time %dms: Process %s started using the CPU for %dms burst ",
                           t, proc->id, proc->remaining);
                } else {
                    printf("time %dms: Process %s started using the CPU for remaining %dms of %dms burst ",
                           t, proc->id, proc->remaining, total);
                }
                print_rq();
                printf("\n");
            }
            Event de = { t + proc->remaining, EV_CPU_DONE, pi, proc->event_token };
            ev_push(de);
            break;
        }

        case EV_CPU_DONE: {
            int actual = proc->cpu_bursts[proc->burst_idx];
            int bursts_left = proc->num_bursts - proc->burst_idx - 1;

            cpu_busy += (long)(t - proc->cpu_start_time);
            proc->total_ta += (long)(t - proc->ready_at);
            proc->bursts_done++;

            if (t <= PRINT_LIMIT) {
                printf("time %dms: Process %s completed a CPU burst; %d burst%s to go ",
                       t, proc->id, bursts_left, bursts_left == 1 ? "" : "s");
                print_rq();
                printf("\n");
            }

            if (!opt)
                proc->tau = ceil(p.alpha * actual + (1.0 - p.alpha) * proc->tau);

            if (bursts_left > 0) {
                int io_done = t + half + proc->io_bursts[proc->burst_idx];
                if (t <= PRINT_LIMIT) {
                    printf("time %dms: Process %s switching out of CPU; "
                           "blocking on I/O until time %dms ",
                           t, proc->id, io_done);
                    print_rq();
                    printf("\n");
                }
                Event ie = { io_done, EV_IO_DONE, pi, 0 };
                ev_push(ie);
            } else {
                if (t <= PRINT_LIMIT) {
                    printf("time %dms: Process %s terminated ", t, proc->id);
                    print_rq();
                    printf("\n");
                }
                sim_end = t + half;
            }

            proc->burst_idx++;
            proc->remaining = bursts_left > 0 ? proc->cpu_bursts[proc->burst_idx] : 0;
            proc->cpu_start_time = 0;
            proc->event_token++;
            cpu_free_at = t + half;
            cpu_running = -1;

            if (!rq_empty())
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;
        }

        case EV_IO_DONE:
            proc->ready_at = t;
            proc->remaining = proc->cpu_bursts[proc->burst_idx];
            rq_push_sorted(pi, opt, preemptive);
            if (preemptive && srt_should_preempt(t, pi, cpu_running, opt)) {
                srt_preempt(t, pi, "completed I/O", half, opt, &cpu_running,
                            &cpu_free_at, &cpu_busy);
            } else {
                if (t <= PRINT_LIMIT) {
                    printf("time %dms: Process %s completed I/O; added to ready queue ",
                           t, proc->id);
                    print_rq();
                    printf("\n");
                }
            }
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_SLICE_EXP:
            break;
        }
    }

    printf("time %dms: Simulator ended for %s [Q: -]\n", sim_end, name);
    collect_stats(sp, n, sim_end, cpu_busy, out);
}

static void run_stub(SimProc *sp, int n, AlgoType algo, SimParams p,
                     SimStats *out) {
    (void)n; (void)p;
    int opt = (p.alpha <= 0.0);
    const char *name = algo_name(algo, opt);
    printf("time 0ms: Simulator started for %s [Q: -]\n", name);
    printf("time 0ms: Simulator ended for %s [Q: -]\n", name);
    (void)sp;
    memset(out, 0, sizeof(*out));
}

/* ── public entry point ──────────────────────────────────────────────── */
void run_simulation(const Process *procs, int n, AlgoType algo,
                    SimParams params, SimStats *out) {
    /* copy process data into SimProc array */
    SimProc *sp = malloc(n * sizeof(SimProc));
    if (!sp) { fprintf(stderr, "ERROR: malloc failed\n"); exit(EXIT_FAILURE); }

    for (int i = 0; i < n; i++) {
        const Process *src = &procs[i];
        memset(&sp[i], 0, sizeof(sp[i]));
        strncpy(sp[i].id, src->id, sizeof(sp[i].id));
        sp[i].cpu_bound    = src->cpu_bound;
        sp[i].arrival_time = src->arrival_time;
        sp[i].num_bursts   = src->num_bursts;
        sp[i].cpu_bursts   = src->cpu_bursts;
        sp[i].io_bursts    = src->io_bursts;
        sp[i].burst_idx    = 0;
        sp[i].remaining    = src->cpu_bursts[0];
    }

    switch (algo) {
    case ALGO_FCFS:
        run_fcfs(sp, n, params, out);
        break;
    case ALGO_SJF:
    case ALGO_SRT:
        run_sjf_srt(sp, n, algo, params, out);
        break;
    default:
        run_stub(sp, n, algo, params, out);
        break;
    }

    free(sp);
}
