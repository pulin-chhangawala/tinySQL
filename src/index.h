/*
 * index.h - B-tree index for faster lookups
 *
 * A simple B-tree index that maps string keys to row indices.
 * Used to speed up WHERE clause lookups and JOIN operations
 * when an index exists on the join column.
 *
 * This is a simplified B-tree (really more of a sorted array
 * with binary search), but it demonstrates the concept of index
 * structures and why they make queries faster.
 */

#ifndef INDEX_H
#define INDEX_H

#include <stdbool.h>

#define BTREE_ORDER 64  /* max keys per node */

typedef struct btree_entry {
    char key[256];
    int row_index;
} btree_entry_t;

typedef struct btree_node {
    btree_entry_t entries[BTREE_ORDER];
    int num_entries;
    struct btree_node *children[BTREE_ORDER + 1];
    bool is_leaf;
} btree_node_t;

typedef struct {
    btree_node_t *root;
    int num_entries;
    char column_name[64];
    char table_name[64];
} btree_index_t;

/* create an index on a column */
btree_index_t *index_create(const char *table_name, const char *column_name);

/* insert a key-rowindex pair */
void index_insert(btree_index_t *idx, const char *key, int row_index);

/* lookup: returns row index, or -1 if not found */
int index_lookup(btree_index_t *idx, const char *key);

/* range query: returns array of row indices where key is in [lo, hi] */
int *index_range(btree_index_t *idx, const char *lo, const char *hi, int *count);

/* print index stats */
void index_print_stats(const btree_index_t *idx);

/* free */
void index_destroy(btree_index_t *idx);

#endif /* INDEX_H */
