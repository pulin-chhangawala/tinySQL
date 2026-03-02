#define _GNU_SOURCE
#include "hashjoin.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * DJB2 hash function: Dan Bernstein's simple string hash.
 * Not cryptographic, but fast and has good distribution for
 * typical string keys. Good enough for a hash join.
 */
static unsigned int djb2_hash(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = (unsigned char)*str++))
        hash = ((hash << 5) + hash) + c;
    return hash;
}

static unsigned int hash_key(const char *key) {
    return djb2_hash(key) % HASH_BUCKETS;
}

hash_join_table_t *hashjoin_build(const table_t *table, int join_col_idx) {
    hash_join_table_t *ht = calloc(1, sizeof(hash_join_table_t));
    ht->build_table = table;
    ht->join_col_idx = join_col_idx;

    for (int r = 0; r < table->num_rows; r++) {
        const char *key = table->rows[r].cells[join_col_idx];
        unsigned int bucket = hash_key(key);

        hash_entry_t *entry = malloc(sizeof(hash_entry_t));
        entry->row_index = r;
        entry->next = ht->buckets[bucket];

        if (ht->buckets[bucket]) ht->num_collisions++;
        ht->buckets[bucket] = entry;
        ht->num_entries++;
    }

    return ht;
}

int *hashjoin_probe(const hash_join_table_t *ht, const char *key, int *count) {
    *count = 0;
    unsigned int bucket = hash_key(key);

    /* first pass: count matches */
    int n = 0;
    hash_entry_t *e = ht->buckets[bucket];
    while (e) {
        if (strcasecmp(ht->build_table->rows[e->row_index].cells[ht->join_col_idx],
                       key) == 0) {
            n++;
        }
        e = e->next;
    }

    if (n == 0) return NULL;

    /* second pass: collect matches */
    int *results = malloc(n * sizeof(int));
    *count = 0;
    e = ht->buckets[bucket];
    while (e) {
        if (strcasecmp(ht->build_table->rows[e->row_index].cells[ht->join_col_idx],
                       key) == 0) {
            results[(*count)++] = e->row_index;
        }
        e = e->next;
    }

    return results;
}

void hashjoin_print_stats(const hash_join_table_t *ht) {
    int used_buckets = 0;
    int max_chain = 0;
    for (int i = 0; i < HASH_BUCKETS; i++) {
        if (ht->buckets[i]) {
            used_buckets++;
            int chain = 0;
            hash_entry_t *e = ht->buckets[i];
            while (e) { chain++; e = e->next; }
            if (chain > max_chain) max_chain = chain;
        }
    }

    printf("  Hash Join Stats:\n");
    printf("    Entries: %d\n", ht->num_entries);
    printf("    Buckets used: %d/%d (%.1f%%)\n",
           used_buckets, HASH_BUCKETS,
           used_buckets * 100.0 / HASH_BUCKETS);
    printf("    Collisions: %d\n", ht->num_collisions);
    printf("    Max chain: %d\n", max_chain);
    printf("    Load factor: %.2f\n",
           (double)ht->num_entries / HASH_BUCKETS);
}

void hashjoin_destroy(hash_join_table_t *ht) {
    if (!ht) return;
    for (int i = 0; i < HASH_BUCKETS; i++) {
        hash_entry_t *e = ht->buckets[i];
        while (e) {
            hash_entry_t *next = e->next;
            free(e);
            e = next;
        }
    }
    free(ht);
}
