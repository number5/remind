/***************************************************************/
/*                                                             */
/*  DEDUPE.C                                                   */
/*                                                             */
/*  Code to suppress duplicate reminders                       */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"
#include "types.h"
#include "globals.h"
#include "protos.h"
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

typedef struct dedupe_entry {
    struct hash_link link;
    int trigger_date;
    int trigger_time;
    char const *body;
} DedupeEntry;

static hash_table DedupeTable;

static unsigned int DedupeHashFunc(void *x)
{
    DedupeEntry *e = (DedupeEntry *) x;
    unsigned int hashval = (unsigned int) e->trigger_date;
    if (e->trigger_time != NO_TIME) {
        hashval += (unsigned int) e->trigger_time;
    }
    hashval += HashVal(e->body);
    return hashval;
}

static int CompareDedupes(void *x, void *y)
{
    DedupeEntry *a = (DedupeEntry *) x;
    DedupeEntry *b = (DedupeEntry *) y;
    if (a->trigger_date != b->trigger_date) return 1;
    if (a->trigger_time != b->trigger_time) return 1;
    return strcmp(a->body, b->body);
}

/***************************************************************/
/*                                                             */
/*  FreeDedupeEntry                                            */
/*                                                             */
/*  Free a DedupeEntry object                                  */
/*                                                             */
/***************************************************************/
static void
FreeDedupeEntry(DedupeEntry *e)
{
    if (e->body) {
        free((char *) e->body);
    }
    free(e);
}

/***************************************************************/
/*                                                             */
/*  FindDedupeEntry                                            */
/*                                                             */
/*  Check if we have a dedupe entry for a given date and body  */
/*                                                             */
/***************************************************************/
static DedupeEntry *
FindDedupeEntry(int trigger_date, int trigger_time, char const *body)
{
    DedupeEntry *e;
    DedupeEntry candidate;
    candidate.body = body;
    candidate.trigger_date = trigger_date;
    candidate.trigger_time = trigger_time;
    e = hash_table_find(&DedupeTable, &candidate);
    return e;
}

/***************************************************************/
/*                                                             */
/*  InsertDedupeEntry                                          */
/*                                                             */
/*  Insert a dedupe entry for given date and body.             */
/*                                                             */
/***************************************************************/
static void
InsertDedupeEntry(int trigger_date, int trigger_time, char const *body)
{
    DedupeEntry *e;

    e = malloc(sizeof(DedupeEntry));
    if (!e) {
        return; /* No error checking... what can we do? */
    }
    e->trigger_date = trigger_date;
    e->trigger_time = trigger_time;
    e->body = strdup(body);
    if (!e->body) {
        free(e);
        return;
    }

    hash_table_insert(&DedupeTable, e);
}

/***************************************************************/
/*                                                             */
/*  ShouldDedupe                                               */
/*                                                             */
/*  Returns 1 if we've already issued this exact reminder; 0   */
/*  otherwise.  If it returns 0, remembers that we have seen   */
/*  the reminder                                               */
/*                                                             */
/***************************************************************/
int
ShouldDedupe(int trigger_date, int trigger_time, char const *body)
{
    DedupeEntry *e = FindDedupeEntry(trigger_date, trigger_time, body);

    if (e) {
        return 1;
    }
    InsertDedupeEntry(trigger_date, trigger_time, body);
    return 0;
}

/***************************************************************/
/*                                                             */
/*  ClearDedupeTable                                           */
/*                                                             */
/*  Free all the storage used by the dedupe table              */
/*                                                             */
/***************************************************************/
void
ClearDedupeTable(void)
{
    DedupeEntry *e, *next;

    e = hash_table_next(&DedupeTable, NULL);
    while(e) {
        next = hash_table_next(&DedupeTable, e);
        hash_table_delete_no_resize(&DedupeTable, e);
        FreeDedupeEntry(e);
        e = next;
    }
    hash_table_free(&DedupeTable);
    InitDedupeTable();
}

/***************************************************************/
/*                                                             */
/*  InitDedupeTable                                            */
/*                                                             */
/*  Initialize the dedupe table at program startup             */
/*                                                             */
/***************************************************************/
void
InitDedupeTable(void)
{
    hash_table_init(&DedupeTable,
                    offsetof(DedupeEntry, link),
                    DedupeHashFunc, CompareDedupes);
}

void
get_dedupe_hash_stats(int *total, int *maxlen, double *avglen)
{
    struct hash_table_stats s;
    hash_table_get_stats(&DedupeTable, &s);
    *total = s.num_entries;
    *maxlen = s.max_len;
    *avglen = s.avg_len;
}
