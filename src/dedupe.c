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

#define DEDUPE_HASH_SLOTS 31
typedef struct dedupe_entry {
    struct dedupe_entry *next;
    int trigger_date;
    int trigger_time;
    char const *body;
} DedupeEntry;

static DedupeEntry *DedupeTable[DEDUPE_HASH_SLOTS];

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
/*  GetDedupeBucket                                            */
/*                                                             */
/*  Get the bucket for a given date and body                   */
/*                                                             */
/***************************************************************/
static unsigned int
GetDedupeBucket(int trigger_date, int trigger_time, char const *body)
{
    unsigned int bucket = trigger_date;
    if (trigger_time != NO_TIME) {
        bucket += trigger_time;
    }
    bucket += HashVal(body);
    return bucket % DEDUPE_HASH_SLOTS;
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

    unsigned int bucket = GetDedupeBucket(trigger_date, trigger_time, body);

    e = DedupeTable[bucket];
    while(e) {
        if (e->trigger_date == trigger_date &&
            e->trigger_time == trigger_time &&
            !strcmp(body, e->body)) {
            return e;
        }
        e = e->next;
    }
    return NULL;
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

    unsigned int bucket = GetDedupeBucket(trigger_date, trigger_time, body);

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

    e->next = DedupeTable[bucket];
    DedupeTable[bucket] = e;
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
    for (int i=0; i<DEDUPE_HASH_SLOTS; i++) {
        e = DedupeTable[i];
        while (e) {
            next = e->next;
            FreeDedupeEntry(e);
            e = next;
        }
        DedupeTable[i] = NULL;
    }
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
    for (int i=0; i<DEDUPE_HASH_SLOTS; i++) {
        DedupeTable[i] = NULL;
    }
}

void
get_dedupe_hash_stats(int *total, int *maxlen, double *avglen)
{
    int len;
    int i;
    DedupeEntry *e;

    *maxlen = 0;
    *total = 0;
    for (i=0; i<DEDUPE_HASH_SLOTS; i++) {
        len = 0;
        e = DedupeTable[i];
        while (e) {
            len++;
            (*total)++;
            e = e->next;
        }
        if (len > *maxlen) {
            *maxlen = len;
        }
    }
    *avglen = (double) *total / (double) DEDUPE_HASH_SLOTS;
}
