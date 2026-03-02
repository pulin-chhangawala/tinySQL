/*
 * planner.h - Query plan generation and visualization
 *
 * Translates a parsed query AST into an execution plan, then
 * pretty-prints it as a tree. This makes it easier to understand
 * what the engine is actually doing under the hood:
 *
 *   tinysql> .explain SELECT name FROM students WHERE gpa > 3.5
 *   Project [name]
 *   └── Filter [gpa > 3.5]
 *       └── TableScan [students]
 */

#ifndef PLANNER_H
#define PLANNER_H

#include "parser.h"

typedef enum {
    PLAN_TABLE_SCAN,
    PLAN_FILTER,
    PLAN_PROJECT,
    PLAN_SORT,
    PLAN_LIMIT,
    PLAN_HASH_JOIN,
    PLAN_NESTED_LOOP_JOIN,
    PLAN_AGGREGATE,
    PLAN_DISTINCT,
} plan_node_type_t;

typedef struct plan_node {
    plan_node_type_t type;
    char description[256];
    int estimated_rows;
    struct plan_node *child;       /* left / only child */
    struct plan_node *right_child; /* for joins */
} plan_node_t;

/* generate a query plan from a parsed query */
plan_node_t *plan_create(const query_t *q);

/* print the plan as a tree */
void plan_print(const plan_node_t *plan, int depth);

/* free plan tree */
void plan_destroy(plan_node_t *plan);

#endif /* PLANNER_H */
