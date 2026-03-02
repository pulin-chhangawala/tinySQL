#define _GNU_SOURCE
#include "executor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <strings.h>

void exec_init(exec_context_t *ctx, const char *data_dir) {
    memset(ctx, 0, sizeof(*ctx));
    strncpy(ctx->data_dir, data_dir, sizeof(ctx->data_dir) - 1);
}

void exec_destroy(exec_context_t *ctx) {
    for (int i = 0; i < ctx->num_tables; i++)
        table_destroy(ctx->tables[i]);
}

table_t *exec_load_table(exec_context_t *ctx, const char *name) {
    /* check if already loaded */
    table_t *existing = exec_find_table(ctx, name);
    if (existing) return existing;

    /* try to find <data_dir>/<name>.csv */
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.csv", ctx->data_dir, name);

    table_t *t = table_load_csv(path, name);
    if (!t) {
        /* also try without .csv extension */
        snprintf(path, sizeof(path), "%s/%s", ctx->data_dir, name);
        t = table_load_csv(path, name);
    }
    if (!t) return NULL;

    if (ctx->num_tables < MAX_LOADED_TABLES) {
        ctx->tables[ctx->num_tables++] = t;
    }
    return t;
}

table_t *exec_find_table(exec_context_t *ctx, const char *name) {
    for (int i = 0; i < ctx->num_tables; i++) {
        if (strcasecmp(ctx->tables[i]->name, name) == 0)
            return ctx->tables[i];
    }
    return NULL;
}

/* --- helpers --- */

/* try to parse as number; returns 1 if numeric */
static int try_parse_number(const char *s, double *out) {
    if (!s || !s[0]) return 0;
    char *end;
    *out = strtod(s, &end);
    return (end != s && *end == '\0');
}

/* evaluate a condition against a row */
static int eval_condition(const condition_t *cond, const row_t *row,
                          const table_t *t) {
    int col_idx = table_find_column(t, cond->left.name);
    if (col_idx < 0) return 0;

    const char *cell = row->cells[col_idx];
    double cell_num, val_num;
    int both_numeric = try_parse_number(cell, &cell_num) &&
                       try_parse_number(cond->str_value, &val_num);

    switch (cond->op) {
    case CMP_EQ:
        return both_numeric ? (cell_num == val_num) :
               (strcasecmp(cell, cond->str_value) == 0);
    case CMP_NEQ:
        return both_numeric ? (cell_num != val_num) :
               (strcasecmp(cell, cond->str_value) != 0);
    case CMP_LT:
        return both_numeric ? (cell_num < val_num) :
               (strcasecmp(cell, cond->str_value) < 0);
    case CMP_GT:
        return both_numeric ? (cell_num > val_num) :
               (strcasecmp(cell, cond->str_value) > 0);
    case CMP_LTE:
        return both_numeric ? (cell_num <= val_num) :
               (strcasecmp(cell, cond->str_value) <= 0);
    case CMP_GTE:
        return both_numeric ? (cell_num >= val_num) :
               (strcasecmp(cell, cond->str_value) >= 0);
    case CMP_LIKE: {
        /* basic LIKE: only handle % at start/end */
        const char *pattern = cond->str_value;
        int plen = strlen(pattern);
        int clen = strlen(cell);
        if (plen >= 2 && pattern[0] == '%' && pattern[plen-1] == '%') {
            /* %substring% */
            char sub[256];
            strncpy(sub, pattern + 1, plen - 2);
            sub[plen - 2] = '\0';
            return (strcasestr(cell, sub) != NULL);
        } else if (pattern[0] == '%') {
            /* %suffix */
            const char *suffix = pattern + 1;
            int slen = strlen(suffix);
            return (clen >= slen &&
                    strcasecmp(cell + clen - slen, suffix) == 0);
        } else if (pattern[plen-1] == '%') {
            /* prefix% */
            return (strncasecmp(cell, pattern, plen - 1) == 0);
        } else {
            return (strcasecmp(cell, pattern) == 0);
        }
    }
    }
    return 0;
}

/* evaluate all WHERE conditions (simple AND logic for now) */
static int eval_conditions(const query_t *q, const row_t *row,
                           const table_t *t) {
    if (q->num_conditions == 0) return 1;

    int result = eval_condition(&q->conditions[0], row, t);
    for (int i = 1; i < q->num_conditions; i++) {
        int cond_result = eval_condition(&q->conditions[i], row, t);
        /* previous condition's logical connector determines how to combine */
        if (q->conditions[i-1].logical == TOK_OR)
            result = result || cond_result;
        else
            result = result && cond_result;
    }
    return result;
}

/* comparison function for qsort, used by ORDER BY */
typedef struct {
    int col_idx;
    int descending;
    const table_t *table;
} sort_ctx_t;

static sort_ctx_t sort_context;

static int compare_rows(const void *a, const void *b) {
    const row_t *ra = (const row_t *)a;
    const row_t *rb = (const row_t *)b;
    int ci = sort_context.col_idx;

    double na, nb;
    int both_num = try_parse_number(ra->cells[ci], &na) &&
                   try_parse_number(rb->cells[ci], &nb);

    int cmp;
    if (both_num) {
        cmp = (na > nb) - (na < nb);
    } else {
        cmp = strcasecmp(ra->cells[ci], rb->cells[ci]);
    }

    return sort_context.descending ? -cmp : cmp;
}

/* --- main execution --- */

/*
 * Execute a simple SELECT (no JOIN, no GROUP BY).
 * This handles: SELECT, WHERE, ORDER BY, LIMIT, DISTINCT
 */
static table_t *exec_simple_select(const table_t *source, const query_t *q,
                                    char *err, int err_size) {
    /* figure out output columns */
    const char *out_col_names[MAX_TABLE_COLS];
    int out_col_indices[MAX_TABLE_COLS];
    int num_out_cols = 0;

    if (q->num_columns == 1 && q->columns[0].is_star && q->columns[0].agg == AGG_NONE) {
        /* SELECT * */
        num_out_cols = source->num_columns;
        for (int i = 0; i < num_out_cols; i++) {
            out_col_names[i] = source->columns[i].name;
            out_col_indices[i] = i;
        }
    } else {
        for (int i = 0; i < q->num_columns; i++) {
            if (q->columns[i].agg != AGG_NONE) continue; /* handle aggregates separately */
            int ci = table_find_column(source, q->columns[i].name);
            if (ci < 0) {
                snprintf(err, err_size, "column '%s' not found in table '%s'",
                         q->columns[i].name, source->name);
                return NULL;
            }
            out_col_names[num_out_cols] = q->columns[i].alias[0] ?
                                          q->columns[i].alias : source->columns[ci].name;
            out_col_indices[num_out_cols] = ci;
            num_out_cols++;
        }
    }

    /* check for aggregation without GROUP BY */
    int has_agg = 0;
    for (int i = 0; i < q->num_columns; i++) {
        if (q->columns[i].agg != AGG_NONE) { has_agg = 1; break; }
    }

    if (has_agg && q->num_group_by == 0) {
        /* aggregate over entire table */
        const char *agg_col_names[MAX_TABLE_COLS];
        int n_agg = 0;

        for (int i = 0; i < q->num_columns; i++) {
            if (q->columns[i].agg == AGG_NONE) continue;
            const char *alias = q->columns[i].alias[0] ? q->columns[i].alias : q->columns[i].name;
            agg_col_names[n_agg++] = alias;
        }

        table_t *result = table_create("result", agg_col_names, n_agg);
        row_t agg_row;
        memset(&agg_row, 0, sizeof(agg_row));

        int agg_idx = 0;
        for (int i = 0; i < q->num_columns; i++) {
            if (q->columns[i].agg == AGG_NONE) continue;

            if (q->columns[i].agg == AGG_COUNT && q->columns[i].is_star) {
                /* COUNT(*): just count matching rows */
                int count = 0;
                for (int r = 0; r < source->num_rows; r++) {
                    if (eval_conditions(q, &source->rows[r], source))
                        count++;
                }
                snprintf(agg_row.cells[agg_idx], MAX_CELL_LEN, "%d", count);
            } else {
                int ci = table_find_column(source, q->columns[i].name);
                if (ci < 0) {
                    snprintf(err, err_size, "column '%s' not found", q->columns[i].name);
                    table_destroy(result);
                    return NULL;
                }

                double sum = 0, min_v = 1e18, max_v = -1e18;
                int count = 0;

                for (int r = 0; r < source->num_rows; r++) {
                    if (!eval_conditions(q, &source->rows[r], source)) continue;
                    double v;
                    if (try_parse_number(source->rows[r].cells[ci], &v)) {
                        sum += v;
                        if (v < min_v) min_v = v;
                        if (v > max_v) max_v = v;
                        count++;
                    }
                }

                switch (q->columns[i].agg) {
                    case AGG_COUNT: snprintf(agg_row.cells[agg_idx], MAX_CELL_LEN, "%d", count); break;
                    case AGG_SUM:   snprintf(agg_row.cells[agg_idx], MAX_CELL_LEN, "%.2f", sum); break;
                    case AGG_AVG:   snprintf(agg_row.cells[agg_idx], MAX_CELL_LEN, "%.2f", count > 0 ? sum/count : 0); break;
                    case AGG_MIN:   snprintf(agg_row.cells[agg_idx], MAX_CELL_LEN, "%.2f", min_v); break;
                    case AGG_MAX:   snprintf(agg_row.cells[agg_idx], MAX_CELL_LEN, "%.2f", max_v); break;
                    default: break;
                }
            }
            agg_idx++;
        }
        table_add_row(result, &agg_row);
        return result;
    }

    /* non-aggregate: filter + project */
    table_t *result = table_create("result", out_col_names, num_out_cols);

    for (int r = 0; r < source->num_rows; r++) {
        if (!eval_conditions(q, &source->rows[r], source)) continue;

        row_t out_row;
        memset(&out_row, 0, sizeof(out_row));
        for (int c = 0; c < num_out_cols; c++) {
            strncpy(out_row.cells[c], source->rows[r].cells[out_col_indices[c]],
                    MAX_CELL_LEN - 1);
        }
        table_add_row(result, &out_row);
    }

    /* ORDER BY */
    if (q->num_order_keys > 0) {
        int oi = table_find_column(result, q->order_keys[0].column);
        if (oi >= 0) {
            sort_context.col_idx = oi;
            sort_context.descending = q->order_keys[0].descending;
            sort_context.table = result;
            qsort(result->rows, result->num_rows, sizeof(row_t), compare_rows);
        }
    }

    /* DISTINCT */
    if (q->distinct && result->num_rows > 1) {
        int write_idx = 1;
        for (int r = 1; r < result->num_rows; r++) {
            int dup = 1;
            for (int c = 0; c < result->num_columns && dup; c++) {
                if (strcmp(result->rows[r].cells[c],
                           result->rows[write_idx - 1].cells[c]) != 0)
                    dup = 0;
            }
            if (!dup) {
                if (write_idx != r)
                    memcpy(&result->rows[write_idx], &result->rows[r], sizeof(row_t));
                write_idx++;
            }
        }
        result->num_rows = write_idx;
    }

    return result;
}

/*
 * Execute a JOIN query. We do a simple nested-loop join.
 * Not efficient, but correct and clear.
 */
static table_t *exec_join_select(exec_context_t *ctx, const query_t *q,
                                  char *err, int err_size) {
    table_t *left = exec_load_table(ctx, q->from_table.name);
    if (!left) {
        snprintf(err, err_size, "table '%s' not found", q->from_table.name);
        return NULL;
    }

    table_t *right = exec_load_table(ctx, q->join.table.name);
    if (!right) {
        snprintf(err, err_size, "table '%s' not found", q->join.table.name);
        return NULL;
    }

    /* figure out join columns */
    const char *left_join_col = q->join.left_col.name;
    const char *right_join_col = q->join.right_col.name;

    int lji = table_find_column(left, left_join_col);
    int rji = table_find_column(right, right_join_col);

    /* if table-qualified, try the other table */
    if (lji < 0 && q->join.left_col.table[0]) {
        if (strcasecmp(q->join.left_col.table, q->join.table.name) == 0 ||
            strcasecmp(q->join.left_col.table, q->join.table.alias) == 0) {
            lji = table_find_column(right, left_join_col);
            rji = table_find_column(left, right_join_col);
        }
    }

    if (lji < 0) lji = table_find_column(left, left_join_col);
    if (rji < 0) rji = table_find_column(right, right_join_col);

    if (lji < 0 || rji < 0) {
        snprintf(err, err_size, "join column not found");
        return NULL;
    }

    /* build combined column list */
    const char *col_names[MAX_TABLE_COLS];
    int n_cols = 0;
    for (int i = 0; i < left->num_columns; i++)
        col_names[n_cols++] = left->columns[i].name;
    for (int i = 0; i < right->num_columns; i++)
        col_names[n_cols++] = right->columns[i].name;

    table_t *joined = table_create("joined", col_names, n_cols);

    /* nested loop join */
    for (int i = 0; i < left->num_rows; i++) {
        int matched = 0;
        for (int j = 0; j < right->num_rows; j++) {
            if (strcasecmp(left->rows[i].cells[lji],
                           right->rows[j].cells[rji]) == 0) {
                row_t row;
                memset(&row, 0, sizeof(row));
                int c = 0;
                for (int k = 0; k < left->num_columns; k++)
                    strncpy(row.cells[c++], left->rows[i].cells[k], MAX_CELL_LEN - 1);
                for (int k = 0; k < right->num_columns; k++)
                    strncpy(row.cells[c++], right->rows[j].cells[k], MAX_CELL_LEN - 1);
                table_add_row(joined, &row);
                matched = 1;
            }
        }
        /* LEFT JOIN: include unmatched left rows */
        if (!matched && q->join.is_left_join) {
            row_t row;
            memset(&row, 0, sizeof(row));
            int c = 0;
            for (int k = 0; k < left->num_columns; k++)
                strncpy(row.cells[c++], left->rows[i].cells[k], MAX_CELL_LEN - 1);
            /* right side stays empty (NULL) */
            table_add_row(joined, &row);
        }
    }

    /* now run the rest of the query (SELECT, WHERE, ORDER BY) on joined */
    table_t *result = exec_simple_select(joined, q, err, err_size);
    table_destroy(joined);
    return result;
}

table_t *exec_query(exec_context_t *ctx, const query_t *q,
                    char *err, int err_size) {
    err[0] = '\0';

    if (q->has_join) {
        return exec_join_select(ctx, q, err, err_size);
    }

    /* load the source table */
    table_t *source = exec_load_table(ctx, q->from_table.name);
    if (!source) {
        snprintf(err, err_size,
                 "table '%s' not found (looking for %s/%s.csv)",
                 q->from_table.name, ctx->data_dir, q->from_table.name);
        return NULL;
    }

    return exec_simple_select(source, q, err, err_size);
}
