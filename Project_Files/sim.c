#include "sim.h"
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

typedef struct { int time; EvType type; int pidx; } Event;

#define MAX_EV 600000
static Event   g_heap[MAX_EV];
static int     g_hsize;
static SimProc *g_sp;   /* set before each simulation run */

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
    Event se = { start, EV_CPU_START, next };
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

/* ── FCFS simulation ─────────────────────────────────────────────────── */
static void run_fcfs(SimProc *sp, int n, SimParams p, SimStats *out) {
    const int half = p.t_cs / 2;
    g_sp = sp;

    /* seed the event heap */
    g_hsize = 0;
    rq_init();
    for (int i = 0; i < n; i++) {
        Event e = { sp[i].arrival_time, EV_ARRIVE, i };
        ev_push(e);
    }

    int cpu_running = -1;  /* index of running process; -1 = idle         */
    int cpu_free_at = 0;   /* earliest time CPU is free after switch-out   */
    int sim_end     = 0;
    long cpu_busy   = 0;   /* total ms CPU was executing                   */

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
            printf("time %dms: Process %s arrived; added to ready queue ", t, proc->id);
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_CPU_START:
            proc->remaining      = proc->cpu_bursts[proc->burst_idx];
            proc->cpu_start_time = t;
            proc->total_wait    += (long)(t - half - proc->ready_at);
            proc->num_cs++;
            printf("time %dms: Process %s started using the CPU for %dms burst ",
                   t, proc->id, proc->remaining);
            print_rq();
            printf("\n");
            {
                Event de = { t + proc->remaining, EV_CPU_DONE, pi };
                ev_push(de);
            }
            break;

        case EV_CPU_DONE: {
            cpu_busy += (long)(t - proc->cpu_start_time);
            int bursts_left = proc->num_bursts - proc->burst_idx - 1;
            proc->total_ta += (long)(t - proc->ready_at);
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
                Event ie = { io_done, EV_IO_DONE, pi };
                ev_push(ie);
            } else {
                printf("time %dms: Process %s terminated ", t, proc->id);
                print_rq();
                printf("\n");
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
            printf("time %dms: Process %s completed I/O; added to ready queue ",
                   t, proc->id);
            print_rq();
            printf("\n");
            if (cpu_running == -1)
                do_dispatch(t, cpu_free_at, half, &cpu_running);
            break;

        case EV_SLICE_EXP:
            break; /* not used in FCFS */
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
    default:
        run_stub(sp, n, algo, params, out);
        break;
    }

    free(sp);
}
