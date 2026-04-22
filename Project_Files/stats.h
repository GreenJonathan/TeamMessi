#ifndef STATS_H
#define STATS_H

#include <stdio.h>
#include "process.h"
#include "sim.h"

void write_simout(FILE *fp, const Process *procs, int n, int ncpu,
                  const char *const algo_names[4], const SimStats stats[4]);

#endif /* STATS_H */
