#define _GNU_SOURCE
#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 256

table_t *table_create(const char *name, const char **col_names, int num_cols) {
    table_t *t = calloc(1, sizeof(table_t));
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->num_columns = num_cols;
    for (int i = 0; i < num_cols; i++)
        strncpy(t->columns[i].name, col_names[i], MAX_COL_NAME - 1);

    t->row_capacity = INITIAL_CAPACITY;
    t->rows = calloc(t->row_capacity, sizeof(row_t));
    t->num_rows = 0;
    return t;
}

void table_add_row(table_t *t, const row_t *row) {
    if (t->num_rows >= t->row_capacity) {
        t->row_capacity *= 2;
        t->rows = realloc(t->rows, t->row_capacity * sizeof(row_t));
    }
    memcpy(&t->rows[t->num_rows], row, sizeof(row_t));
    t->num_rows++;
}

int table_find_column(const table_t *t, const char *col_name) {
    for (int i = 0; i < t->num_columns; i++) {
        /* case-insensitive comparison */
        if (strcasecmp(t->columns[i].name, col_name) == 0)
            return i;
    }
    return -1;
}

/*
 * CSV parsing. Not trying to handle every edge case (no RFC 4180
 * compliance), but it handles quoted fields and commas within quotes.
 */
static int parse_csv_line(const char *line, char fields[][MAX_CELL_LEN], int max_fields) {
    int field_idx = 0;
    int char_idx = 0;
    bool in_quotes = false;

    for (int i = 0; line[i] && line[i] != '\n' && line[i] != '\r'; i++) {
        char c = line[i];

        if (in_quotes) {
            if (c == '"') {
                /* check for escaped quote ("") */
                if (line[i+1] == '"') {
                    fields[field_idx][char_idx++] = '"';
                    i++;
                } else {
                    in_quotes = false;
                }
            } else {
                if (char_idx < MAX_CELL_LEN - 1)
                    fields[field_idx][char_idx++] = c;
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields[field_idx][char_idx] = '\0';
                field_idx++;
                char_idx = 0;
                if (field_idx >= max_fields) break;
            } else {
                if (char_idx < MAX_CELL_LEN - 1)
                    fields[field_idx][char_idx++] = c;
            }
        }
    }
    fields[field_idx][char_idx] = '\0';
    return field_idx + 1;
}

table_t *table_load_csv(const char *filepath, const char *table_name) {
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "error: cannot open '%s'\n", filepath);
        return NULL;
    }

    char line[4096];

    /* first line = column headers */
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return NULL;
    }

    char headers[MAX_TABLE_COLS][MAX_CELL_LEN];
    memset(headers, 0, sizeof(headers));
    int num_cols = parse_csv_line(line, headers, MAX_TABLE_COLS);

    const char *col_names[MAX_TABLE_COLS];
    for (int i = 0; i < num_cols; i++)
        col_names[i] = headers[i];

    table_t *t = table_create(table_name, col_names, num_cols);

    /* read data rows */
    while (fgets(line, sizeof(line), fp)) {
        /* skip empty lines */
        if (line[0] == '\n' || line[0] == '\r') continue;

        row_t row;
        memset(&row, 0, sizeof(row));
        parse_csv_line(line, row.cells, MAX_TABLE_COLS);
        table_add_row(t, &row);
    }

    fclose(fp);
    return t;
}

/* figure out good column widths for display */
static void compute_col_widths(const table_t *t, int *widths, int limit) {
    for (int c = 0; c < t->num_columns; c++) {
        widths[c] = strlen(t->columns[c].name);
    }

    int n = limit >= 0 && limit < t->num_rows ? limit : t->num_rows;
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < t->num_columns; c++) {
            int len = strlen(t->rows[r].cells[c]);
            if (len > widths[c]) widths[c] = len;
        }
    }

    /* cap at reasonable width */
    for (int c = 0; c < t->num_columns; c++) {
        if (widths[c] > 40) widths[c] = 40;
    }
}

void table_print(const table_t *t, int limit) {
    if (!t || t->num_columns == 0) {
        printf("(empty table)\n");
        return;
    }

    int widths[MAX_TABLE_COLS];
    compute_col_widths(t, widths, limit);

    /* header */
    printf("| ");
    for (int c = 0; c < t->num_columns; c++)
        printf("%-*s | ", widths[c], t->columns[c].name);
    printf("\n|");
    for (int c = 0; c < t->num_columns; c++) {
        printf("-");
        for (int j = 0; j < widths[c]; j++) printf("-");
        printf("-|");
    }
    printf("\n");

    /* rows */
    int n = limit >= 0 && limit < t->num_rows ? limit : t->num_rows;
    for (int r = 0; r < n; r++) {
        printf("| ");
        for (int c = 0; c < t->num_columns; c++)
            printf("%-*s | ", widths[c], t->rows[r].cells[c]);
        printf("\n");
    }

    printf("(%d row%s)\n", n, n == 1 ? "" : "s");
}

void table_destroy(table_t *t) {
    if (!t) return;
    free(t->rows);
    free(t);
}
