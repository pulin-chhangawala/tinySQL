/*
 * mutate.h - Data mutation operations (INSERT, CREATE TABLE, DELETE)
 *
 * Unlike SELECT (which is read-only), these operations modify the
 * underlying data. Changes are persisted back to CSV files.
 *
 * The big design decision: we keep everything in CSV because this
 * is meant to be simple and inspectable. A real database would use
 * pages and a WAL, but for learning purposes, CSV is transparent.
 */

#ifndef MUTATE_H
#define MUTATE_H

#include "table.h"
#include "executor.h"

/* INSERT INTO table (col1, col2, ...) VALUES (val1, val2, ...) */
int exec_insert(exec_context_t *ctx, const char *input,
                char *err, int err_size);

/* CREATE TABLE name (col1, col2, ...) */
int exec_create_table(exec_context_t *ctx, const char *input,
                      char *err, int err_size);

/* DELETE FROM table WHERE condition */
int exec_delete(exec_context_t *ctx, const char *input,
                char *err, int err_size);

/* write table to CSV (persist changes) */
int table_save_csv(const table_t *table, const char *dir);

#endif /* MUTATE_H */
