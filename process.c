#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "process.h"

/* Generate a single exponential deviate <= upper_bound using drand48(). */
static double next_exp(double lambda, int upper_bound)
{
    double r, x;
    do {
        r = drand48();
        if (r == 0.0) continue;
        x = -log(r) / lambda;
    } while (x > upper_bound);
    return x;
}

Process *generate_processes(int n, int ncpu, double lambda, int upper_bound)
{
    Process *procs = malloc(n * sizeof(Process));
    if (!procs) {
        fprintf(stderr, "ERROR: malloc failed\n");
        exit(EXIT_FAILURE);
    }

    int nio = n - ncpu; /* number of I/O-bound processes */

    for (int i = 0; i < n; i++) {
        Process *p = &procs[i];

        /* Assign ID: letter = i/10, digit = i%10 */
        p->id[0] = 'A' + (i / 10);
        p->id[1] = '0' + (i % 10);
        p->id[2] = '\0';

        /* I/O-bound first (indices 0..nio-1), CPU-bound after (indices nio..n-1) */
        p->cpu_bound = (i >= nio) ? 1 : 0;

        /* 1. Arrival time */
        p->arrival_time = (int)floor(next_exp(lambda, upper_bound));

        /* 2. Number of bursts: uniform 1-16 */
        p->num_bursts = (int)(drand48() * 16) + 1;

        /* 3. Allocate burst arrays */
        p->cpu_bursts = malloc(p->num_bursts * sizeof(int));
        p->io_bursts  = malloc((p->num_bursts - 1) * sizeof(int));
        if (!p->cpu_bursts || (p->num_bursts > 1 && !p->io_bursts)) {
            fprintf(stderr, "ERROR: malloc failed\n");
            exit(EXIT_FAILURE);
        }

        /* 4. Generate burst times */
        for (int b = 0; b < p->num_bursts; b++) {
            /* CPU burst */
            int cpu_t = (int)ceil(next_exp(lambda, upper_bound));
            if (p->cpu_bound) cpu_t *= 6;
            p->cpu_bursts[b] = cpu_t;

            /* I/O burst (skip for last burst) */
            if (b < p->num_bursts - 1) {
                int io_t = (int)ceil(next_exp(lambda, upper_bound));
                if (!p->cpu_bound) io_t *= 8;
                p->io_bursts[b] = io_t;
            }
        }
    }

    return procs;
}

void free_processes(Process *procs, int n)
{
    for (int i = 0; i < n; i++) {
        free(procs[i].cpu_bursts);
        free(procs[i].io_bursts);
    }
    free(procs);
}
