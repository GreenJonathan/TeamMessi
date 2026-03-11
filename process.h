#ifndef PROCESS_H
#define PROCESS_H

typedef struct {
    char id[4];       /* e.g. "A0", "B3" */
    int cpu_bound;    /* 1 = CPU-bound, 0 = I/O-bound */
    int arrival_time; /* floor of exponential value */
    int num_bursts;   /* uniform random 1-16 */
    int *cpu_bursts;  /* array of num_bursts CPU burst times */
    int *io_bursts;   /* array of num_bursts-1 I/O burst times */
} Process;

/* Generate n processes: first (n-ncpu) are I/O-bound, last ncpu are CPU-bound.
   lambda and upper_bound control the exponential RNG.
   Returns heap-allocated array of n Process structs (caller must free). */
Process *generate_processes(int n, int ncpu, double lambda, int upper_bound);

/* Free all memory associated with a process array of length n. */
void free_processes(Process *procs, int n);

#endif /* PROCESS_H */
