/***************************************************************/
/*                                                             */
/*  HASHTAB_STATS.C                                            */
/*                                                             */
/*  Utility function to print hash table stats.                */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/**
 * \file hashtab_stats.c
 * \brief Obtain or print statistics about a hash table
 *
 * NOTE: Use of any of the functions in this file will require linking
 * with the math library to pull in the sqrt() function.
 */

#include "hashtab.h"
#include <stdio.h>
#include <math.h>

/**
 * \brief Dump hash table statistics to a stdio FILE
 *
 * \param t A pointer to a hash_table object
 * \param fp A stdio file pointer that is writable
 */
void
hash_table_dump_stats(hash_table *t, FILE *fp)
{
    struct hash_table_stats stat;
    hash_table_get_stats(t, &stat);
    fprintf(fp, "#Entries: %lu\n#Buckets: %lu\n#Non-empty Buckets: %lu\n",
            (unsigned long) stat.num_entries,
            (unsigned long) stat.num_buckets,
            (unsigned long) stat.num_nonempty_buckets);
    fprintf(fp, "Max len: %lu\nMin len: %lu\nAvg len: %.4f\nStd dev: %.4f\nAvg nonempty len: %.4f\n",
            (unsigned long) stat.max_len,
            (unsigned long) stat.min_len,
            stat.avg_len, stat.stddev, stat.avg_nonempty_len);
}

/**
 * \brief Obtain hash table statistics
 *
 * This function fills in the elements of a struct hash_table_stats object
 * with hash table statistics.
 *
 * \param t A pointer to a hash_table object
 * \param stat A pointer to a hash_table_stats object that will be filled in
 */
void
hash_table_get_stats(hash_table *t, struct hash_table_stats *stat)
{
    size_t n = hash_table_num_buckets(t);
    size_t max_len = 0;
    size_t min_len = 1000000000;

    stat->num_buckets = n;
    stat->num_entries = hash_table_num_entries(t);
    stat->max_len = 0;
    stat->min_len = 0;
    stat->avg_len = 0.0;
    stat->stddev  = 0.0;
    stat->num_nonempty_buckets = 0;
    stat->avg_nonempty_len = 0.0;
    double sum = 0.0;
    double sumsq = 0.0;

    if (n == 0) {
        return;
    }

    for (size_t i=0; i<n; i++) {
        size_t c = hash_table_chain_len(t, i);
        if (c != 0) {
            stat->num_nonempty_buckets++;
        }
        sum += (double) c;
        sumsq += (double) c * (double) c;
        if (c > max_len) max_len = c;
        if (c < min_len) min_len = c;
    }
    double avg_len = sum / (double) n;
    double stddev = sqrt( (sumsq / (double) n) - (avg_len * avg_len) );
    if (stat->num_nonempty_buckets > 0) {
        stat->avg_nonempty_len = sum / (double) stat->num_nonempty_buckets;
    }
    stat->max_len = max_len;
    stat->min_len = min_len;
    stat->avg_len = avg_len;
    stat->stddev  = stddev;
}
