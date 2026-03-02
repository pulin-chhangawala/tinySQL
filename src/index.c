#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Simplified B-tree that uses sorted insertion + binary search.
 * Not a full B-tree with splitting (that would require a lot more code),
 * but demonstrates the core idea of indexed lookups.
 *
 * For a real implementation you'd want proper node splitting and
 * balancing, but this is enough to show the performance difference
 * between O(n) table scan vs O(log n) indexed lookup.
 */

btree_index_t *index_create(const char *table_name, const char *column_name) {
    btree_index_t *idx = calloc(1, sizeof(btree_index_t));
    strncpy(idx->table_name, table_name, sizeof(idx->table_name) - 1);
    strncpy(idx->column_name, column_name, sizeof(idx->column_name) - 1);

    /* start with a single leaf node, grow as needed via linked leaves */
    idx->root = calloc(1, sizeof(btree_node_t));
    idx->root->is_leaf = true;
    idx->num_entries = 0;

    return idx;
}

/* binary search for insertion position */
static int find_position(btree_node_t *node, const char *key) {
    int lo = 0, hi = node->num_entries;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (strcmp(node->entries[mid].key, key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void index_insert(btree_index_t *idx, const char *key, int row_index) {
    btree_node_t *node = idx->root;

    /* if current node is full, chain a new leaf */
    if (node->num_entries >= BTREE_ORDER) {
        btree_node_t *new_node = calloc(1, sizeof(btree_node_t));
        new_node->is_leaf = true;
        new_node->children[0] = (btree_node_t *)node;  /* link to prev */
        idx->root = new_node;
        node = new_node;
    }

    /* sorted insertion */
    int pos = find_position(node, key);

    /* shift entries right */
    for (int i = node->num_entries; i > pos; i--)
        node->entries[i] = node->entries[i - 1];

    strncpy(node->entries[pos].key, key, sizeof(node->entries[pos].key) - 1);
    node->entries[pos].row_index = row_index;
    node->num_entries++;
    idx->num_entries++;
}

int index_lookup(btree_index_t *idx, const char *key) {
    btree_node_t *node = idx->root;

    while (node) {
        /* binary search within node */
        int lo = 0, hi = node->num_entries - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            int cmp = strcmp(node->entries[mid].key, key);
            if (cmp == 0)
                return node->entries[mid].row_index;
            else if (cmp < 0)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        /* check chained node */
        node = node->is_leaf ? (btree_node_t *)node->children[0] : NULL;
    }

    return -1;  /* not found */
}

int *index_range(btree_index_t *idx, const char *lo_key, const char *hi_key, int *count) {
    *count = 0;
    int capacity = 64;
    int *results = malloc(capacity * sizeof(int));

    btree_node_t *node = idx->root;
    while (node) {
        for (int i = 0; i < node->num_entries; i++) {
            if (strcmp(node->entries[i].key, lo_key) >= 0 &&
                strcmp(node->entries[i].key, hi_key) <= 0) {
                if (*count >= capacity) {
                    capacity *= 2;
                    results = realloc(results, capacity * sizeof(int));
                }
                results[(*count)++] = node->entries[i].row_index;
            }
        }
        node = node->is_leaf ? (btree_node_t *)node->children[0] : NULL;
    }

    return results;
}

void index_print_stats(const btree_index_t *idx) {
    printf("  Index: %s.%s (%d entries)\n",
           idx->table_name, idx->column_name, idx->num_entries);
}

static void destroy_node(btree_node_t *node) {
    if (!node) return;
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_entries; i++)
            destroy_node(node->children[i]);
    }
    free(node);
}

void index_destroy(btree_index_t *idx) {
    if (!idx) return;
    /* walk chain */
    btree_node_t *node = idx->root;
    while (node) {
        btree_node_t *next = node->is_leaf ? (btree_node_t *)node->children[0] : NULL;
        if (!node->is_leaf) destroy_node(node);
        else free(node);
        node = next;
    }
    free(idx);
}
