#ifndef TABLE_H
#define TABLE_H

#include <stdbool.h>

/*
 * In-memory table representation. Rows are stored as arrays of
 * string values (everything is text internally; we cast to numbers
 * for comparisons and aggregations).
 */

#define MAX_TABLE_COLS 32
#define MAX_COL_NAME   64
#define MAX_CELL_LEN   256

typedef struct {
    char name[MAX_COL_NAME];
} col_def_t;

typedef struct {
    char cells[MAX_TABLE_COLS][MAX_CELL_LEN];
} row_t;

typedef struct {
    char name[64];
    col_def_t columns[MAX_TABLE_COLS];
    int num_columns;

    row_t *rows;
    int num_rows;
    int row_capacity;
} table_t;

/* load a table from CSV file */
table_t *table_load_csv(const char *filepath, const char *table_name);

/* create an empty table with given column names */
table_t *table_create(const char *name, const char **col_names, int num_cols);

/* add a row (copies the data) */
void table_add_row(table_t *t, const row_t *row);

/* find column index by name, returns -1 if not found */
int table_find_column(const table_t *t, const char *col_name);

/* print table in a nice formatted way */
void table_print(const table_t *t, int limit);

/* free a table */
void table_destroy(table_t *t);

#endif /* TABLE_H */
