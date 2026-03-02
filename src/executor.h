#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "parser.h"
#include "table.h"

/*
 * Executes a parsed query against loaded tables.
 * Returns a new table with the result set (caller must free it).
 */

#define MAX_LOADED_TABLES 8

typedef struct {
    table_t *tables[MAX_LOADED_TABLES];
    int num_tables;
    char data_dir[512];   /* directory to look for CSV files */
} exec_context_t;

void exec_init(exec_context_t *ctx, const char *data_dir);
void exec_destroy(exec_context_t *ctx);

/* load a CSV into the context (registers it by table name) */
table_t *exec_load_table(exec_context_t *ctx, const char *name);

/* find a loaded table by name */
table_t *exec_find_table(exec_context_t *ctx, const char *name);

/* execute a query, returns result table. NULL on error. */
table_t *exec_query(exec_context_t *ctx, const query_t *q, 
                    char *err, int err_size);

#endif /* EXECUTOR_H */
