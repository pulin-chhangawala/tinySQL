/*
 * hashjoin.h - Hash-based join implementation
 *
 * Replaces the O(n*m) nested-loop join with an O(n+m) hash join
 * for equi-join conditions. The build phase loads the smaller table
 * into a hash table, then the probe phase scans the larger table
 * and looks up matches in O(1) per row.
 *
 * This is the same algorithm PostgreSQL uses for its "Hash Join"
 * plan node. The key insight is that join performance depends on
 * the hash table fitting in memory, which for CSV files, it always will.
 */

#ifndef HASHJOIN_H
#define HASHJOIN_H

#include "table.h"

#define HASH_BUCKETS 1024

typedef struct hash_entry {
    int row_index;             /* index into the build table */
    struct hash_entry *next;   /* chaining for collisions */
} hash_entry_t;

typedef struct {
    hash_entry_t *buckets[HASH_BUCKETS];
    const table_t *build_table;
    int join_col_idx;
    int num_entries;
    int num_collisions;
} hash_join_table_t;

/* build phase: construct hash table from the smaller table */
hash_join_table_t *hashjoin_build(const table_t *table, int join_col_idx);

/* probe phase: find all rows in build table matching a key */
int *hashjoin_probe(const hash_join_table_t *ht, const char *key, int *count);

/* stats */
void hashjoin_print_stats(const hash_join_table_t *ht);

/* cleanup */
void hashjoin_destroy(hash_join_table_t *ht);

#endif /* HASHJOIN_H */
