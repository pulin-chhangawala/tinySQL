#define _GNU_SOURCE
#include "mutate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

/* ---- helpers ---- */

/* skip whitespace */
static const char *skip_ws(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* read an identifier or quoted string */
static const char *read_token(const char *s, char *out, int max) {
    s = skip_ws(s);
    int i = 0;

    if (*s == '\'') {
        /* quoted string */
        s++;
        while (*s && *s != '\'' && i < max - 1)
            out[i++] = *s++;
        if (*s == '\'') s++;
    } else {
        /* bare identifier */
        while (*s && !isspace((unsigned char)*s) && *s != ',' &&
               *s != ')' && *s != '(' && i < max - 1) {
            out[i++] = *s++;
        }
    }
    out[i] = '\0';
    return s;
}

/* ---- INSERT INTO table (cols) VALUES (vals) ---- */

int exec_insert(exec_context_t *ctx, const char *input,
                char *err, int err_size) {
    /* parse: INSERT INTO <table> (col1, col2) VALUES (val1, val2) */
    /* or:    INSERT INTO <table> VALUES (val1, val2) */
    const char *p = input;

    /* skip INSERT */
    p = skip_ws(p);
    if (strncasecmp(p, "INSERT", 6) != 0) {
        snprintf(err, err_size, "expected INSERT");
        return -1;
    }
    p += 6;

    /* skip INTO */
    p = skip_ws(p);
    if (strncasecmp(p, "INTO", 4) != 0) {
        snprintf(err, err_size, "expected INTO after INSERT");
        return -1;
    }
    p += 4;

    /* table name */
    char table_name[64];
    p = read_token(p, table_name, sizeof(table_name));

    /* load or find table */
    table_t *t = exec_load_table(ctx, table_name);
    if (!t) {
        snprintf(err, err_size, "table '%s' not found", table_name);
        return -1;
    }

    /* optional column list */
    p = skip_ws(p);
    char col_names[MAX_TABLE_COLS][64];
    int num_cols = 0;
    int has_col_list = 0;

    if (*p == '(') {
        has_col_list = 1;
        p++;
        while (*p && *p != ')') {
            p = skip_ws(p);
            p = read_token(p, col_names[num_cols], 64);
            num_cols++;
            p = skip_ws(p);
            if (*p == ',') p++;
        }
        if (*p == ')') p++;
    }

    /* VALUES */
    p = skip_ws(p);
    if (strncasecmp(p, "VALUES", 6) != 0) {
        snprintf(err, err_size, "expected VALUES");
        return -1;
    }
    p += 6;

    /* value list */
    p = skip_ws(p);
    if (*p != '(') {
        snprintf(err, err_size, "expected '(' after VALUES");
        return -1;
    }
    p++;

    char values[MAX_TABLE_COLS][MAX_CELL_LEN];
    int num_vals = 0;
    while (*p && *p != ')') {
        p = skip_ws(p);
        p = read_token(p, values[num_vals], MAX_CELL_LEN);
        num_vals++;
        p = skip_ws(p);
        if (*p == ',') p++;
    }

    /* verify column count */
    if (has_col_list && num_vals != num_cols) {
        snprintf(err, err_size, "column count (%d) != value count (%d)",
                 num_cols, num_vals);
        return -1;
    }
    if (!has_col_list && num_vals != t->num_columns) {
        snprintf(err, err_size, "value count (%d) != table columns (%d)",
                 num_vals, t->num_columns);
        return -1;
    }

    /* build row */
    row_t row;
    memset(&row, 0, sizeof(row));

    if (has_col_list) {
        /* map columns to positions */
        for (int i = 0; i < num_cols; i++) {
            int ci = table_find_column(t, col_names[i]);
            if (ci < 0) {
                snprintf(err, err_size, "column '%s' not found", col_names[i]);
                return -1;
            }
            strncpy(row.cells[ci], values[i], MAX_CELL_LEN - 1);
        }
    } else {
        for (int i = 0; i < num_vals; i++)
            strncpy(row.cells[i], values[i], MAX_CELL_LEN - 1);
    }

    table_add_row(t, &row);

    /* persist to disk */
    table_save_csv(t, ctx->data_dir);

    return 1;  /* 1 row inserted */
}

/* ---- CREATE TABLE name (col1, col2, ...) ---- */

int exec_create_table(exec_context_t *ctx, const char *input,
                      char *err, int err_size) {
    const char *p = input;

    p = skip_ws(p);
    if (strncasecmp(p, "CREATE", 6) != 0) {
        snprintf(err, err_size, "expected CREATE");
        return -1;
    }
    p += 6;

    p = skip_ws(p);
    if (strncasecmp(p, "TABLE", 5) != 0) {
        snprintf(err, err_size, "expected TABLE after CREATE");
        return -1;
    }
    p += 5;

    /* table name */
    char table_name[64];
    p = read_token(p, table_name, sizeof(table_name));

    /* check if already exists */
    if (exec_find_table(ctx, table_name)) {
        snprintf(err, err_size, "table '%s' already exists", table_name);
        return -1;
    }

    /* column list */
    p = skip_ws(p);
    if (*p != '(') {
        snprintf(err, err_size, "expected '(' after table name");
        return -1;
    }
    p++;

    const char *col_names[MAX_TABLE_COLS];
    char col_bufs[MAX_TABLE_COLS][64];
    int num_cols = 0;

    while (*p && *p != ')') {
        p = skip_ws(p);
        p = read_token(p, col_bufs[num_cols], 64);
        col_names[num_cols] = col_bufs[num_cols];
        num_cols++;
        p = skip_ws(p);
        if (*p == ',') p++;
    }

    if (num_cols == 0) {
        snprintf(err, err_size, "no columns specified");
        return -1;
    }

    table_t *t = table_create(table_name, col_names, num_cols);

    if (ctx->num_tables < MAX_LOADED_TABLES) {
        ctx->tables[ctx->num_tables++] = t;
    }

    /* persist empty CSV with header */
    table_save_csv(t, ctx->data_dir);

    return 0;
}

/* ---- DELETE FROM table WHERE col = val ---- */

int exec_delete(exec_context_t *ctx, const char *input,
                char *err, int err_size) {
    const char *p = input;

    p = skip_ws(p);
    if (strncasecmp(p, "DELETE", 6) != 0) {
        snprintf(err, err_size, "expected DELETE");
        return -1;
    }
    p += 6;

    p = skip_ws(p);
    if (strncasecmp(p, "FROM", 4) != 0) {
        snprintf(err, err_size, "expected FROM after DELETE");
        return -1;
    }
    p += 4;

    char table_name[64];
    p = read_token(p, table_name, sizeof(table_name));

    table_t *t = exec_load_table(ctx, table_name);
    if (!t) {
        snprintf(err, err_size, "table '%s' not found", table_name);
        return -1;
    }

    /* parse WHERE clause (simple: col OP val only) */
    p = skip_ws(p);
    if (strncasecmp(p, "WHERE", 5) != 0) {
        /* DELETE without WHERE: delete all rows */
        int deleted = t->num_rows;
        t->num_rows = 0;
        table_save_csv(t, ctx->data_dir);
        return deleted;
    }
    p += 5;

    /* parse "col = val" */
    char col_name[64], value[MAX_CELL_LEN];
    p = read_token(p, col_name, sizeof(col_name));

    p = skip_ws(p);
    char op = *p++;  /* = or other */
    (void)op;        /* for now, only support = */

    p = read_token(p, value, sizeof(value));

    int ci = table_find_column(t, col_name);
    if (ci < 0) {
        snprintf(err, err_size, "column '%s' not found", col_name);
        return -1;
    }

    /* delete matching rows (compact in-place) */
    int deleted = 0;
    int write_idx = 0;
    for (int r = 0; r < t->num_rows; r++) {
        if (strcasecmp(t->rows[r].cells[ci], value) == 0) {
            deleted++;
        } else {
            if (write_idx != r) {
                memcpy(&t->rows[write_idx], &t->rows[r], sizeof(row_t));
            }
            write_idx++;
        }
    }
    t->num_rows = write_idx;

    table_save_csv(t, ctx->data_dir);
    return deleted;
}

/* ---- persist table to CSV ---- */

int table_save_csv(const table_t *table, const char *dir) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.csv", dir, table->name);

    FILE *f = fopen(path, "w");
    if (!f) return -1;

    /* header */
    for (int c = 0; c < table->num_columns; c++) {
        if (c > 0) fprintf(f, ",");
        fprintf(f, "%s", table->columns[c].name);
    }
    fprintf(f, "\n");

    /* rows */
    for (int r = 0; r < table->num_rows; r++) {
        for (int c = 0; c < table->num_columns; c++) {
            if (c > 0) fprintf(f, ",");
            /* quote if contains comma */
            if (strchr(table->rows[r].cells[c], ',')) {
                fprintf(f, "\"%s\"", table->rows[r].cells[c]);
            } else {
                fprintf(f, "%s", table->rows[r].cells[c]);
            }
        }
        fprintf(f, "\n");
    }

    fclose(f);
    return 0;
}
