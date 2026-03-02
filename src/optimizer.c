#define _GNU_SOURCE
#include "optimizer.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/*
 * Cost model (simplified):
 *
 * Nested-loop join: O(|L| * |R|)
 * Hash join: O(|L| + |R|) for build + probe, plus memory for hash table
 * Index lookup: O(log n) per lookup
 *
 * We estimate costs in "page accesses" which is a rough proxy for
 * how much work the query will do.
 */

static int estimate_table_size(exec_context_t *ctx, const char *name) {
    table_t *t = exec_find_table(ctx, name);
    if (t) return t->num_rows;
    /* haven't loaded yet, peek at the CSV */
    return 100;  /* default estimate */
}

optimizer_result_t optimize_query(exec_context_t *ctx, const query_t *q) {
    optimizer_result_t opt;
    memset(&opt, 0, sizeof(opt));

    char *expl = opt.explanation;
    int expl_size = sizeof(opt.explanation);
    int offset = 0;

    if (!q->has_join) {
        /* no join: simple scan, nothing to optimize */
        int n = estimate_table_size(ctx, q->from_table.name);
        opt.estimated_cost = n;
        offset += snprintf(expl + offset, expl_size - offset,
                 "Simple scan on '%s' (~%d rows)\n",
                 q->from_table.name, n);

        if (q->num_conditions > 0) {
            /* estimate selectivity: wild guess of 30% */
            int filtered = n * 3 / 10;
            offset += snprintf(expl + offset, expl_size - offset,
                     "  WHERE reduces to ~%d rows (est. 30%% selectivity)\n",
                     filtered);
        }

        opt.predicate_pushdown = 0;
        return opt;
    }

    /* JOIN optimization */
    int left_size = estimate_table_size(ctx, q->from_table.name);
    int right_size = estimate_table_size(ctx, q->join.table.name);

    int nl_cost = left_size * right_size;   /* nested loop */
    int hj_cost = left_size + right_size;   /* hash join */

    offset += snprintf(expl + offset, expl_size - offset,
             "Join: '%s' (%d rows) × '%s' (%d rows)\n",
             q->from_table.name, left_size,
             q->join.table.name, right_size);

    offset += snprintf(expl + offset, expl_size - offset,
             "  Nested Loop cost: ~%d\n", nl_cost);
    offset += snprintf(expl + offset, expl_size - offset,
             "  Hash Join cost:   ~%d\n", hj_cost);

    /* choose hash join if it's cheaper (almost always is for equi-joins) */
    if (hj_cost < nl_cost) {
        opt.use_hash_join = 1;
        opt.estimated_cost = hj_cost;
        offset += snprintf(expl + offset, expl_size - offset,
                 "  → Choosing Hash Join (%.1fx faster)\n",
                 (double)nl_cost / hj_cost);

        /* build side: pick smaller table */
        if (right_size <= left_size) {
            opt.build_side = 1;  /* build on right, probe left */
            offset += snprintf(expl + offset, expl_size - offset,
                     "  → Building hash table on '%s' (smaller)\n",
                     q->join.table.name);
        } else {
            opt.build_side = 0;  /* build on left, probe right */
            offset += snprintf(expl + offset, expl_size - offset,
                     "  → Building hash table on '%s' (smaller)\n",
                     q->from_table.name);
        }
    } else {
        opt.use_hash_join = 0;
        opt.estimated_cost = nl_cost;
        offset += snprintf(expl + offset, expl_size - offset,
                 "  → Using Nested Loop Join\n");
    }

    /* predicate pushdown: can any WHERE conditions be applied
     * before the join? A condition can be pushed down if it only
     * references columns from one table. */
    if (q->num_conditions > 0) {
        int pushable = 0;
        for (int i = 0; i < q->num_conditions; i++) {
            const char *col = q->conditions[i].left.name;
            table_t *lt = exec_find_table(ctx, q->from_table.name);
            table_t *rt = exec_find_table(ctx, q->join.table.name);

            int in_left = lt && table_find_column(lt, col) >= 0;
            int in_right = rt && table_find_column(rt, col) >= 0;

            if ((in_left && !in_right) || (!in_left && in_right)) {
                pushable++;
            }
        }

        if (pushable > 0) {
            opt.predicate_pushdown = 1;
            offset += snprintf(expl + offset, expl_size - offset,
                     "  → Pushing %d predicate(s) below join\n", pushable);
        }
    }

    return opt;
}

void optimizer_explain(const optimizer_result_t *opt) {
    printf("\n  Query Optimizer:\n");
    printf("    Estimated cost: %d\n", opt->estimated_cost);
    printf("    Strategy: %s\n",
           opt->use_hash_join ? "Hash Join" : "Nested Loop Join");
    if (opt->predicate_pushdown)
        printf("    Optimization: Predicate pushdown applied\n");
    printf("\n  Reasoning:\n");

    /* print explanation line by line with indent */
    const char *p = opt->explanation;
    while (*p) {
        printf("    ");
        while (*p && *p != '\n') {
            putchar(*p++);
        }
        putchar('\n');
        if (*p == '\n') p++;
    }
}
