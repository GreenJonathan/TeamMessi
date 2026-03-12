#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "process.h"

/* keeps generating random numbers until we get one thats in range */
static double next_exp(double lambda, int upper_bound)
{
    double r, x;
    do {
        r = drand48();
        if (r == 0.0) continue; /* cant take log of 0 so skip it */
        x = -log(r) / lambda;
    } while (x > upper_bound); /* throw it out if its too big */
    return x;
}

Process *generate_processes(int n, int ncpu, double lambda, int upper_bound)
{
    Process *procs = malloc(n * sizeof(Process));
    if (!procs) {
        fprintf(stderr, "ERROR: malloc failed\n");
        exit(EXIT_FAILURE);
    }

    int nio = n - ncpu; /* how many are I/O-bound */

    for (int i = 0; i < n; i++) {
        Process *p = &procs[i];

        /* id is like A0, A1, ... B0, B1 etc. letter goes up every 10 */
        p->id[0] = 'A' + (i / 10);
        p->id[1] = '0' + (i % 10);
        p->id[2] = '\0';

        /* I/O-bound ones come first, CPU-bound ones come after */
        p->cpu_bound = (i >= nio) ? 1 : 0;

        /* arrival time is just the floor of the exponential value */
        p->arrival_time = (int)floor(next_exp(lambda, upper_bound));

        /* random number of bursts between 1 and 16 */
        p->num_bursts = (int)(drand48() * 16) + 1;

        /* allocate space for the burst arrays */
        p->cpu_bursts = malloc(p->num_bursts * sizeof(int));
        p->io_bursts  = malloc((p->num_bursts - 1) * sizeof(int));
        if (!p->cpu_bursts || (p->num_bursts > 1 && !p->io_bursts)) {
            fprintf(stderr, "ERROR: malloc failed\n");
            exit(EXIT_FAILURE);
        }

        /* fill in cpu and io burst times for each burst */
        for (int b = 0; b < p->num_bursts; b++) {
            /* cpu bursts are longer for cpu-bound processes */
            int cpu_t = (int)ceil(next_exp(lambda, upper_bound));
            if (p->cpu_bound) cpu_t *= 6;
            p->cpu_bursts[b] = cpu_t;

            /* no I/O burst after the last cpu burst */
            if (b < p->num_bursts - 1) {
                int io_t = (int)ceil(next_exp(lambda, upper_bound));
                if (!p->cpu_bound) io_t *= 8; /* I/O-bound processes spend more time doing I/O */
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
