/***************************************************************/
/*                                                             */
/*  DEDUPE.C                                                   */
/*                                                             */
/*  Code to suppress duplicate reminders                       */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
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
    DedupeEntry const *e = (DedupeEntry const *) x;
    unsigned int hashval = (unsigned int) e->trigger_date;
    if (e->trigger_time != NO_TIME) {
        hashval += (unsigned int) e->trigger_time;
    }
    hashval += HashVal_preservecase(e->body);
    return hashval;
}

static int CompareDedupes(void *x, void *y)
{
    DedupeEntry const *a = (DedupeEntry const *) x;
    DedupeEntry const *b = (DedupeEntry const *) y;
    if (a->trigger_date != b->trigger_date) return a->trigger_date - b->trigger_date;
    if (a->trigger_time != b->trigger_time) return a->trigger_time - b->trigger_time;
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
    DedupeEntry const *e = FindDedupeEntry(trigger_date, trigger_time, body);

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
    if (hash_table_init(&DedupeTable,
                        offsetof(DedupeEntry, link),
                        DedupeHashFunc, CompareDedupes) < 0) {
        fprintf(ErrFp, "Unable to initialize function hash table: Out of memory.  Exiting.\n");
        exit(1);
    }
}

void
dump_dedupe_hash_stats(void)
{
    hash_table_dump_stats(&DedupeTable, ErrFp);
}
