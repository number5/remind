/***************************************************************/
/*                                                             */
/*  HASHTAB.H                                                  */
/*                                                             */
/*  Header file for hash-table related functions.              */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/* For size_t */
#include <stdio.h>

/**
 * \brief A structure for holding hash table chain links.
 *
 * This structure is embedded in a container structure to make up
 * a hash table entry
 */
struct hash_link {
    void *next;            /**< Link to next item in the chain */
    unsigned int hashval;  /**< Cached hash function value */
};

/**
 * \brief A hash table
 */
typedef struct {
    unsigned int bucket_choice_index;  /**< Index into array of possible bucket counts */
    size_t num_growths;       /**< How many times have we grown the hash table? */
    size_t num_shrinks;       /**< How many times have we grown the hash table? */
    size_t num_entries;       /**< Number of entries in the hash table */
    size_t hash_link_offset;  /**< Offset of the struct hash_link in the container */
    void **buckets;           /**< Array of buckets */
    unsigned int (*hashfunc)(void const *x); /**< Pointer to the hashing function */
    int (*compare)(void const *a, void const *b); /**< Pointer to the comparison function */
} hash_table;

/**
 * \brief Data type to hold statistics about a hash table
 */
struct hash_table_stats {
    size_t num_entries;  /**< Number of items in the hash table */
    size_t num_buckets;  /**< Number of buckets in the hash table */
    size_t num_nonempty_buckets; /**< Number of non-emptry buckets */
    size_t max_len;      /**< Length of longest chain in the hash table */
    size_t min_len;      /**< Length of the shortest chain in the hash table */
    size_t num_growths;       /**< How many times have we grown the hash table? */
    size_t num_shrinks;       /**< How many times have we grown the hash table? */
    double avg_len;      /**< Average chain length */
    double avg_nonempty_len; /**< Average chain length of non-empty bucket */
    double stddev;       /**< Standard deviation of chain lengths */
};

int hash_table_init(hash_table *t,
                    size_t link_offset,
                    unsigned int (*hashfunc)(void const *x),
                    int (*compare)(void const *a, void const *b));
void hash_table_free(hash_table *t);
size_t hash_table_num_entries(hash_table const *t);
size_t hash_table_num_buckets(hash_table const *t);
size_t hash_table_chain_len(hash_table *t, size_t i);
int hash_table_insert(hash_table *t, void *item);
void *hash_table_find(hash_table *t, void *candidate);
int hash_table_delete(hash_table *t, void *item);
int hash_table_delete_no_resize(hash_table *t, void *item);
void *hash_table_next(hash_table *t, void *cur);
void hash_table_dump_stats(hash_table *t, FILE *fp);

/**
 * \brief Iterate over all items in a hash table
 *
 * This macro iterates over all items in a hash table.  Here is an
 * example of how to use it:
 *
 *     hash_table tab;
 *     void *item;
 *     hash_table_for_each(item, &tab) {
 *         // Do something with item
 *     }
 */
#define hash_table_for_each(item, t)            \
    for ((item) = hash_table_next((t), NULL);   \
         (item);                                \
         (item) = hash_table_next((t), (item)))

