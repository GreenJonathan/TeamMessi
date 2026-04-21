#ifndef PROCESS_H
#define PROCESS_H

typedef struct {
    char id[4];       /* like "A0" or "B3" */
    int cpu_bound;    /* 1 if cpu-bound, 0 if I/O-bound */
    int arrival_time; /* when the process shows up */
    int num_bursts;   /* how many cpu bursts it has (1-16) */
    int *cpu_bursts;  /* array of all the cpu burst times */
    int *io_bursts;   /* array of I/O burst times, one less than cpu bursts */
} Process;

/* makes all n processes and returns them as an array */
Process *generate_processes(int n, int ncpu, double lambda, int upper_bound);

/* frees everything so we dont leak memory */
void free_processes(Process *procs, int n);

#endif /* PROCESS_H */
