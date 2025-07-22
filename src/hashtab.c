/***************************************************************/
/*                                                             */
/*  HASHTAB.C                                                  */
/*                                                             */
/*  Implementation of hash table.                              */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/**
 * \file hashtab.c
 *
 * \brief Implementation of hash table
 *
 * A hash table manages an array of buckets, each of which is the
 * head of a singly-linked list.  A given hash table can store items
 * of a given type.  The items in a hash table must be structs, and one
 * of their members must be a struct hash_link object.  For example,
 * a hash table containing integers might have the hash objects
 * defined as:
 *
 *     struct int_object {
 *         int value;
 *         struct hash_link link;
 *     };
 *
 * When you initialize the hash table, you pass in the offset to the hash
 * link.  For example, to initialize a hash table designed to hold
 * int_objects, you'd do something like:
 *
 *     unsigned int hash_int_obj(void *x) {
 *         return (unsigned int) ((int_object *) x)->value;
 *     }
 *     int compare_int_obj(void *a, void *b) {
 *         return ((int_object *)a)->value - ((int_object *)b)->value;
 *     }
 *
 *     hash_table tab;
 *     hash_table_init(&tab, offsetof(struct int_object, link), hash_int_obj, compare_int_obj);
 *
 * An item can be in multiple hash tables at once; just declare multiple
 * hash_link members and pass in the appropriate offset to each hash
 * table.
 */

#include "hashtab.h"
#include <stdlib.h>
#include <errno.h>

/*
 * The number of buckets should be a prime number.
 * Use these numbers of buckets to grow or shrink the hash table.
 * Yes, OK, the list below is probably excessive.
 */

/**
 * \brief A list of prime numbers from 17 to about 1.4 billion, approximately
 * doubling with each successive number.
 *
 * These are used as choices for the number of hash buckets in the table
 */
static size_t bucket_choices[] = {
    7, 17, 37, 79, 163, 331, 673, 1361, 2729, 5471, 10949, 21911, 43853, 87719,
    175447, 350899, 701819, 1403641, 2807303, 5614657, 11229331, 22458671,
    44917381, 89834777, 179669557, 359339171, 718678369, 1437356741 };

#define NUM_BUCKET_CHOICES (sizeof(bucket_choices) / sizeof(bucket_choices[0]))

#define NUM_BUCKETS(t) (bucket_choices[t->bucket_choice_index])

#define LINK(t, p) ( (struct hash_link *) (( ((char *) p) + t->hash_link_offset)) )

/**
 * \brief Initialize a hash table
 *
 * Initializes a hash table.  A given hash table can contain a collection
 * of items, all of which must be the same.  An item in a hash table is
 * a structure and one of the elements in the structure must be a
 * struct hash_link object.  For example, if you are storing a collection
 * of integers in a hash table, your item might look like this:
 *
 *     struct item {
 *         int value;
 *         struct hash_link link;
 *     };
 *
 * \param t Pointer to a hash_table object
 * \param link_offset The offset to the struct hash_link object within the object being put in the hash table.  In the example above, it would be
 * offsetof(struct item, link)
 * \param hashfunc A pointer to a function that computes a hash given a pointer to an object.  This function must return an unsigned int.
 * \param compare A pointer to a function that compares two objects.  It must
 * return 0 if they compare equal and non-zero if they do not.
 *
 * \return 0 on success, -1 on failure (and errno is set appropriately)
 */
int
hash_table_init(hash_table *t,
                size_t link_offset,
                unsigned int (*hashfunc)(void const *x),
                int (*compare)(void const *a, void const *b))
{
    t->bucket_choice_index = 0;
    t->num_entries         = 0;
    t->hash_link_offset    = link_offset;
    t->hashfunc            = hashfunc;
    t->compare             = compare;
    t->buckets             = malloc(sizeof(void *) * bucket_choices[0]);
    t->num_growths         = 0;
    t->num_shrinks         = 0;
    if (!t->buckets) {
        return -1;
    }
    for (size_t i=0; i<bucket_choices[0]; i++) {
        t->buckets[i] = NULL;
    }
    return 0;
}

/**
 * \brief Free memory used by a hash table
 *
 * \param t Pointer to a hash_table object
 */
void
hash_table_free(hash_table *t)
{
    free(t->buckets);
    t->buckets = NULL;
    t->bucket_choice_index = -1;
    t->num_entries = 0;
}

/**
 * \brief Return the number of items in a hash table
 *
 * \param t Pointer to a hash_table object
 *
 * \return The number of items in the hash table
 */
size_t
hash_table_num_entries(hash_table const *t)
{
    return t->num_entries;
}

/**
 * \brief Return the number of buckets in a hash table
 *
 * \param t Pointer to a hash_table object
 *
 * \return The number of buckets in the hash table
 */
size_t
hash_table_num_buckets(hash_table const *t)
{
    if (t->bucket_choice_index >= NUM_BUCKET_CHOICES) {
        return 0;
    }

    return NUM_BUCKETS(t);
}

/**
 * \brief Return the length of the i'th bucket chain
 *
 * If i >= num_buckets, returns (size_t) -1
 *
 * \param t Pointer to a hash_table object
 * \param i The bucket whose length we want (0 to num_buckets-1)
 * \return The length of the i'th bucket chain
 */
size_t
hash_table_chain_len(hash_table *t, size_t i)
{
    if (i >= hash_table_num_buckets(t)) {
        return (size_t) -1;
    }
    size_t len = 0;
    void *ptr = t->buckets[i];
    while(ptr) {
        len++;
        ptr = LINK(t, ptr)->next;
    }
    return len;
}

/**
 * \brief Resize a hash table
 *
 * Resizes (either grows or shrinks) a hash table's bucket array
 *
 * \param t Pointer to a hash_table object
 * \param dir Must be either 1 (to increase the bucket array size) or
 * -1 (to decrease it).
 * \return 0 on success, non-zero if resizing fails.  NOTE: Currently, resizing
 * cannot fail; if we fail to allocate memory for the new bucket array,
 * we just keep the existing array.  This behaviour may change in future.
 */
static int
hash_table_resize(hash_table *t, int dir)
{
    if (dir != 1 && dir != -1) {
        return 0;
    }
    if ((dir == -1 && t->bucket_choice_index == 0) ||
        (dir == 1  && t->bucket_choice_index == NUM_BUCKET_CHOICES-1)) {
        return 0;
    }

    size_t num_old_buckets = bucket_choices[t->bucket_choice_index];
    size_t num_new_buckets = bucket_choices[t->bucket_choice_index + dir];

    void **new_buckets = malloc(sizeof(void *) * num_new_buckets);
    if (!new_buckets) {
        /* Out of memory... just don't resize? */
        return 0;
    }
    if (dir == 1) {
        t->num_growths++;
    } else {
        t->num_shrinks++;
    }
    for (size_t j=0; j<num_new_buckets; j++) {
        new_buckets[j] = NULL;
    }

    /* Move everything from the old buckets into the new */
    for (size_t i=0; i<num_old_buckets; i++) {
        void *p = t->buckets[i];
        while(p) {
            struct hash_link *l = LINK(t, p);
            void *nxt = l->next;
            size_t j = l->hashval % num_new_buckets;
            l->next = new_buckets[j];
            new_buckets[j] = p;
            p = nxt;
        }
    }
    free(t->buckets);
    t->buckets = new_buckets;
    t->bucket_choice_index += dir;

    return 0;
}

/**
 * \brief Insert an item into a hash table
 *
 * Inserts an item into a hash table.  The item MUST NOT be freed as
 * long as it is in a hash table
 *
 * \param t Pointer to a hash_table object
 * \param item Pointer to the item to insert
 *
 * \return 0 on success, -1 on failure (and errno is set appropriately)
 */
int
hash_table_insert(hash_table *t, void *item)
{
    if (!item) {
        errno = EINVAL;
        return -1;
    }

    unsigned int v = t->hashfunc(item);

    struct hash_link *l = LINK(t, item);
    l->hashval = v;

    v = v % NUM_BUCKETS(t);

    l->next = t->buckets[v];
    t->buckets[v] = item;
    t->num_entries++;

    /* Grow table for load factor > 2 */
    if (t->bucket_choice_index < NUM_BUCKET_CHOICES-1 &&
        t->num_entries > 2 * NUM_BUCKETS(t)) {
        return hash_table_resize(t, 1);
    }
    return 0;
}

/**
 * \brief Find an item in a hash table
 *
 * \param t Pointer to a hash_table object
 * \param candidate Pointer to an object to be sought in the table
 *
 * \return A pointer to the object if one that matches candidate is found.  NULL if not found
 */
void *
hash_table_find(hash_table *t, void *candidate)
{
    if (!candidate) {
        return NULL;
    }

    unsigned int v = t->hashfunc(candidate);

    void *ptr = t->buckets[v % NUM_BUCKETS(t)];

    while(ptr) {
        if (!t->compare(candidate, ptr)) {
            return ptr;
        }
        ptr = LINK(t, ptr)->next;
    }
    return NULL;
}

/**
 * \brief Delete an item from a hash table
 *
 * \param t Pointer to a hash_table object
 * \param candidate Pointer to an object that is in the table and must be removed from it
 * \param resize_ok If non-zero, then it's OK to resize the hash table.
 *
 * \return 0 on success, -1 on failure
 */
static int
hash_table_delete_helper(hash_table *t, void *item, int resize_ok)
{
    if (!item) {
        errno = EINVAL;
        return -1;
    }

    struct hash_link *l = LINK(t, item);
    unsigned int v = l->hashval;

    v = v % NUM_BUCKETS(t);

    if (t->buckets[v] == item) {
        t->buckets[v] = l->next;
        t->num_entries--;
        if (resize_ok) {
            /* Shrink table for load factor < 1 */
            if (t->bucket_choice_index > 0 &&
                t->num_entries < NUM_BUCKETS(t) / 2) {
                return hash_table_resize(t, -1);
            }
        }
        return 0;
    }

    void *ptr = t->buckets[v];
    while(ptr) {
        struct hash_link *l2 = LINK(t, ptr);
        if (l2->next == item) {
            l2->next = l->next;
            t->num_entries--;
            /* Shrink table for load factor < 1 */
            if (resize_ok) {
                if (t->bucket_choice_index > 0 &&
                    t->num_entries < NUM_BUCKETS(t) / 2) {
                    return hash_table_resize(t, -1);
                }
            }
            return 0;
        }
        ptr = l2->next;
    }

    /* Item not found in hash table */
    errno = ENOENT;
    return -1;
}

int
hash_table_delete(hash_table *t, void *item)
{
    return hash_table_delete_helper(t, item, 1);
}

int
hash_table_delete_no_resize(hash_table *t, void *item)
{
    return hash_table_delete_helper(t, item, 0);
}

/**
 * \brief Iterate to the next item in a hash table
 *
 * Acts as an iterator.  Given a pointer to an item in the hash
 * table, returns the next item, or NULL if no more items.  If the
 * existing-item pointer is supplied as NULL, returns a pointer to the
 * first item in the hash table.  You can therefore iterate across the
 * hash table like this*
 *
 *     void *item = NULL;
 *     while ( (item = hash_table_next(&table, item) ) != NULL) {
 *         // Do something with item
 *     }
 *
 * NOTE that you MUST NOT modify the hash table while iterating over it.
 *
 * \param t Pointer to a hash_table object
 * \param cur The current item.  Supply as NULL to get the first item
 *
 * \return A pointer to the next item in the hash table, or NULL if there
 * are no more items
 */
void *
hash_table_next(hash_table *t, void *cur)
{
    size_t n_buckets = NUM_BUCKETS(t);

    size_t start_bucket = 0;
    if (cur) {
        struct hash_link *l = LINK(t, cur);
        if (l->next) {
            return l->next;
        }
        /* End of this chain; start searching at the next bucket */
        start_bucket = (l->hashval % n_buckets) + 1;
    }

    for (size_t i=start_bucket; i<n_buckets; i++) {
        if (t->buckets[i]) {
            return t->buckets[i];
        }
    }
    return NULL;
}
