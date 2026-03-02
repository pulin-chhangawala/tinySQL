#include "planner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static plan_node_t *make_node(plan_node_type_t type, const char *desc) {
    plan_node_t *n = calloc(1, sizeof(plan_node_t));
    n->type = type;
    strncpy(n->description, desc, sizeof(n->description) - 1);
    n->estimated_rows = -1;  /* unknown */
    return n;
}

plan_node_t *plan_create(const query_t *q) {
    /* bottom-up: start with table scan, layer operators on top */

    /* 1. table scan */
    char desc[256];
    snprintf(desc, sizeof(desc), "TableScan [%s]", q->from_table.name);
    plan_node_t *current = make_node(PLAN_TABLE_SCAN, desc);

    /* 2. join (if present) */
    if (q->has_join) {
        /* decide join strategy based on heuristic:
         * if the join columns look like they'd benefit from hashing,
         * use hash join. Otherwise nested loop. */
        plan_node_type_t jtype = PLAN_HASH_JOIN;

        snprintf(desc, sizeof(desc), "%s [%s.%s = %s.%s]",
                 jtype == PLAN_HASH_JOIN ? "HashJoin" : "NestedLoopJoin",
                 q->join.left_col.table[0] ? q->join.left_col.table : q->from_table.name,
                 q->join.left_col.name,
                 q->join.right_col.table[0] ? q->join.right_col.table : q->join.table.name,
                 q->join.right_col.name);

        plan_node_t *join = make_node(jtype, desc);
        join->child = current;

        snprintf(desc, sizeof(desc), "TableScan [%s]", q->join.table.name);
        join->right_child = make_node(PLAN_TABLE_SCAN, desc);

        current = join;
    }

    /* 3. filter (WHERE) */
    if (q->num_conditions > 0) {
        char conds[256] = "";
        for (int i = 0; i < q->num_conditions; i++) {
            char cond[64];
            const char *ops[] = {"=", "!=", "<", ">", "<=", ">=", "LIKE"};
            snprintf(cond, sizeof(cond), "%s %s %s",
                     q->conditions[i].left.name,
                     ops[q->conditions[i].op],
                     q->conditions[i].str_value);
            if (i > 0) strncat(conds, " AND ", sizeof(conds) - strlen(conds) - 1);
            strncat(conds, cond, sizeof(conds) - strlen(conds) - 1);
        }
        snprintf(desc, sizeof(desc), "Filter [%s]", conds);
        plan_node_t *filter = make_node(PLAN_FILTER, desc);
        filter->child = current;
        current = filter;
    }

    /* 4. aggregation */
    int has_agg = 0;
    for (int i = 0; i < q->num_columns; i++) {
        if (q->columns[i].agg != AGG_NONE) { has_agg = 1; break; }
    }
    if (has_agg) {
        const char *agg_names[] = {"", "COUNT", "SUM", "AVG", "MIN", "MAX"};
        char agg_desc[256] = "";
        for (int i = 0; i < q->num_columns; i++) {
            if (q->columns[i].agg != AGG_NONE) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "%s(%s)",
                         agg_names[q->columns[i].agg], q->columns[i].name);
                if (agg_desc[0]) strncat(agg_desc, ", ", sizeof(agg_desc) - strlen(agg_desc) - 1);
                strncat(agg_desc, tmp, sizeof(agg_desc) - strlen(agg_desc) - 1);
            }
        }
        snprintf(desc, sizeof(desc), "Aggregate [%s]", agg_desc);
        plan_node_t *agg = make_node(PLAN_AGGREGATE, desc);
        agg->child = current;
        current = agg;
    }

    /* 5. sort (ORDER BY) */
    if (q->num_order_keys > 0) {
        snprintf(desc, sizeof(desc), "Sort [%s %s]",
                 q->order_keys[0].column,
                 q->order_keys[0].descending ? "DESC" : "ASC");
        plan_node_t *sort = make_node(PLAN_SORT, desc);
        sort->child = current;
        current = sort;
    }

    /* 6. distinct */
    if (q->distinct) {
        plan_node_t *dist = make_node(PLAN_DISTINCT, "Distinct");
        dist->child = current;
        current = dist;
    }

    /* 7. project (column selection) */
    char cols[256] = "";
    for (int i = 0; i < q->num_columns && i < 8; i++) {
        if (i > 0) strncat(cols, ", ", sizeof(cols) - strlen(cols) - 1);
        if (q->columns[i].agg != AGG_NONE) {
            const char *an[] = {"", "COUNT", "SUM", "AVG", "MIN", "MAX"};
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "%s(%s)",
                     an[q->columns[i].agg], q->columns[i].name);
            strncat(cols, tmp, sizeof(cols) - strlen(cols) - 1);
        } else {
            strncat(cols, q->columns[i].is_star ? "*" : q->columns[i].name,
                    sizeof(cols) - strlen(cols) - 1);
        }
    }
    snprintf(desc, sizeof(desc), "Project [%s]", cols);
    plan_node_t *project = make_node(PLAN_PROJECT, desc);
    project->child = current;
    current = project;

    /* 8. limit */
    if (q->limit >= 0) {
        snprintf(desc, sizeof(desc), "Limit [%d]", q->limit);
        plan_node_t *lim = make_node(PLAN_LIMIT, desc);
        lim->child = current;
        current = lim;
    }

    return current;
}

void plan_print(const plan_node_t *plan, int depth) {
    if (!plan) return;

    /* indentation with tree connectors */
    for (int i = 0; i < depth; i++) {
        if (i == depth - 1)
            printf("└── ");
        else
            printf("    ");
    }

    printf("%s\n", plan->description);

    if (plan->right_child) {
        plan_print(plan->right_child, depth + 1);
    }
    if (plan->child) {
        plan_print(plan->child, depth + 1);
    }
}

void plan_destroy(plan_node_t *plan) {
    if (!plan) return;
    plan_destroy(plan->child);
    plan_destroy(plan->right_child);
    free(plan);
}
