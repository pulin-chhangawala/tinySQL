#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include <stdbool.h>

/*
 * AST node types for our (very) limited SQL grammar.
 *
 * We support:
 *   SELECT [DISTINCT] <columns|*> FROM <table> [JOIN <table> ON <cond>]
 *   [WHERE <conditions>] [GROUP BY <cols>] [ORDER BY <col> ASC|DESC] [LIMIT n]
 *
 * Aggregations: COUNT, SUM, AVG, MIN, MAX
 */

#define MAX_COLUMNS   32
#define MAX_TABLES    4
#define MAX_CONDITIONS 16
#define MAX_ORDER_KEYS 4

/* comparison operators for WHERE clause */
typedef enum {
    CMP_EQ, CMP_NEQ, CMP_LT, CMP_GT, CMP_LTE, CMP_GTE, CMP_LIKE
} cmp_op_t;

/* aggregate functions */
typedef enum {
    AGG_NONE, AGG_COUNT, AGG_SUM, AGG_AVG, AGG_MIN, AGG_MAX
} agg_func_t;

/* a column reference, optionally with alias and aggregation */
typedef struct {
    char table[64];     /* table name or alias (empty = infer) */
    char name[64];      /* column name, or "*" for star */
    char alias[64];     /* AS alias */
    agg_func_t agg;     /* aggregation function, if any */
    bool is_star;
} column_ref_t;

/* a WHERE condition: <column> <op> <value> */
typedef struct {
    column_ref_t left;
    cmp_op_t op;
    char str_value[256];
    double num_value;
    bool is_numeric;
    token_type_t logical; /* TOK_AND or TOK_OR connecting to next */
} condition_t;

/* a table reference, with optional alias */
typedef struct {
    char name[64];       /* file/table name */
    char alias[64];      /* alias for JOINs */
} table_ref_t;

/* JOIN info */
typedef struct {
    table_ref_t table;
    column_ref_t left_col;
    column_ref_t right_col;
    bool is_left_join;
} join_clause_t;

/* ORDER BY entry */
typedef struct {
    char column[64];
    bool descending;
} order_key_t;

/* top-level query AST */
typedef struct {
    /* SELECT */
    column_ref_t columns[MAX_COLUMNS];
    int num_columns;
    bool distinct;

    /* FROM */
    table_ref_t from_table;

    /* JOIN */
    join_clause_t join;
    bool has_join;

    /* WHERE */
    condition_t conditions[MAX_CONDITIONS];
    int num_conditions;

    /* GROUP BY */
    char group_by[MAX_COLUMNS][64];
    int num_group_by;

    /* ORDER BY */
    order_key_t order_keys[MAX_ORDER_KEYS];
    int num_order_keys;

    /* LIMIT */
    int limit;      /* -1 = no limit */
} query_t;

/* parse a SQL string into a query AST. returns 0 on success, -1 on error */
int parse_query(const char *sql, query_t *query, char *error_buf, int error_buf_size);

/* debug print */
void query_print(const query_t *q);

#endif /* PARSER_H */
