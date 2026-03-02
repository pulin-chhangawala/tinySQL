/*
 * optimizer.h - Query optimizer: cost-based plan selection
 *
 * Implements:
 *   1. Predicate pushdown: move WHERE filters before JOINs
 *   2. Join ordering: put the smaller table on the build side
 *   3. Hash join selection: use hash join when possible
 *   4. Index utilization: use B-tree index when available
 *
 * The optimizer transforms the logical plan into a physical plan
 * that executes faster while producing the same results.
 */

#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "parser.h"
#include "executor.h"

typedef struct {
    int use_hash_join;          /* 1 = hash join, 0 = nested loop */
    int build_side;             /* 0 = left builds, 1 = right builds */
    int predicate_pushdown;     /* 1 = filters pushed below join */
    int estimated_cost;         /* estimated I/O cost */
    char explanation[1024];     /* human-readable explanation */
} optimizer_result_t;

/*
 * Analyze a query and produce optimization recommendations.
 * The optimizer doesn't modify the query; it returns suggestions
 * that the executor uses to pick the best strategy.
 */
optimizer_result_t optimize_query(exec_context_t *ctx, const query_t *q);

/* print the optimizer's reasoning */
void optimizer_explain(const optimizer_result_t *opt);

#endif /* OPTIMIZER_H */
