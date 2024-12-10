/***************************************************************/
/*                                                             */
/*  TRANS.C                                                    */
/*                                                             */
/*  Functions to manage the translation table.  Implements     */
/*  the TRANSLATE keyword.                                     */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include "types.h"
#include "globals.h"
#include "protos.h"
#include "err.h"

/* The structure of a translation item */
typedef struct xlat {
    struct hash_link link;
    char *orig;
    char *translated;
} XlateItem;

hash_table TranslationTable;

/***************************************************************/
/*                                                             */
/*  AllocateXlateItem - Allocate a new translation item        */
/*                                                             */
/***************************************************************/
static XlateItem *
AllocateXlateItem(char const *orig, char const *translated)
{
    size_t s1 = sizeof(XlateItem);
    size_t s2 = strlen(orig)+1;
    size_t s3 = strlen(translated)+1;
    XlateItem *item;

    /* Allocate the string space in ONE go! */
    char *blob = malloc(s1+s2+s3);
    if (!blob) {
        return NULL;
    }

    item = (XlateItem *) blob;

    item->orig = blob + s1;
    item->translated = item->orig + s2;
    strcpy(item->orig, orig);
    strcpy(item->translated, translated);
    return item;
}

/***************************************************************/
/*                                                             */
/*  FreeXlateItem - Free a translation item                    */
/*                                                             */
/***************************************************************/
static void
FreeXlateItem(XlateItem *item)
{
    if (!item) return;

    free(item);
}

static void
RemoveTranslation(XlateItem *item)
{
    hash_table_delete(&TranslationTable, item);
    FreeXlateItem(item);
}

static void
RemoveTranslationNoResize(XlateItem *item)
{
    hash_table_delete_no_resize(&TranslationTable, item);
    FreeXlateItem(item);
}

/***************************************************************/
/*                                                             */
/*  ClearTranslationTable - free all translation items         */
/*                                                             */
/***************************************************************/
static void
ClearTranslationTable(void)
{
    XlateItem *item;
    XlateItem *next;

    item = hash_table_next(&TranslationTable, NULL);
    while(item) {
        next = hash_table_next(&TranslationTable, item);
        RemoveTranslationNoResize(item);
        item = next;
    }
    hash_table_free(&TranslationTable);
    InitTranslationTable();
}

void
print_escaped_string(FILE *fp, char const *s)
{
    putc('"', fp);
    while(*s) {
        switch(*s) {
        case '\a': putc('\\', fp); putc('a', fp); break;
        case '\b': putc('\\', fp); putc('b', fp); break;
        case '\f': putc('\\', fp); putc('f', fp); break;
        case '\n': putc('\\', fp); putc('n', fp); break;
        case '\r': putc('\\', fp); putc('r', fp); break;
        case '\t': putc('\\', fp); putc('t', fp); break;
        case '\v': putc('\\', fp); putc('v', fp); break;
        case '"':  putc('\\', fp); putc('"', fp); break;
        case '\\': putc('\\', fp); putc('\\', fp); break;
        default:
            putc(*s, fp); break;
        }
        s++;
    }
    putc('"', fp);
}

/***************************************************************/
/*                                                             */
/*  DumpTranslationTable - Dump the table to a file descriptor */
/*                                                             */
/***************************************************************/
void
DumpTranslationTable(FILE *fp, int json)
{
    XlateItem *item;
    int done = 0;
    if (!json) {
        fprintf(fp, "# Translation table\n");
    } else {
        fprintf(fp, "{");
    }
    item = hash_table_next(&TranslationTable, NULL);
    while(item) {
        if (!json) {
            fprintf(fp, "TRANSLATE ");
            print_escaped_string(fp, item->orig);
            fprintf(fp, " ");
            print_escaped_string(fp, item->translated);
            fprintf(fp, "\n");
        } else {
            if (done) {
                fprintf(fp, ",");
            }
            done=1;
            print_escaped_string(fp, item->orig);
            fprintf(fp, ":");
            print_escaped_string(fp, item->translated);
        }
        item = hash_table_next(&TranslationTable, item);
    }
    if (json) {
        fprintf(fp, "}");
    }
}

static unsigned int
HashXlateItem(void *x)
{
    XlateItem *item = (XlateItem *) x;
    return HashVal_preservecase(item->orig);
}

static int
CompareXlateItems(void *a, void *b)
{
    XlateItem *i = (XlateItem *) a;
    XlateItem *j = (XlateItem *) b;
    return strcmp(i->orig, j->orig);
}

void
InitTranslationTable(void)
{
    if (hash_table_init(&TranslationTable, offsetof(XlateItem, link),
                        HashXlateItem, CompareXlateItems) < 0) {
        fprintf(stderr, "Unable to initialize translation hash table: Out of memory.  Exiting.\n");
        exit(1);
    }
    InsertTranslation("LANGID", "en");
}

static XlateItem *
FindTranslation(char const *orig)
{
    XlateItem *item;
    XlateItem candidate;
    candidate.orig = (char *) orig;
    item = hash_table_find(&TranslationTable, &candidate);
    return item;
}

int
InsertTranslation(char const *orig, char const *translated)
{
    XlateItem *item = FindTranslation(orig);
    if (item) {
        if (!strcmp(item->translated, translated)) {
            /* Translation is the same; do nothing */
            return OK;
        }
        RemoveTranslation(item);
    }
    item = AllocateXlateItem(orig, translated);
    if (!item) {
        return E_NO_MEM;
    }
    hash_table_insert(&TranslationTable, item);
    return OK;
}

char const *
GetTranslatedString(char const *orig)
{
    XlateItem *item = FindTranslation(orig);
    if (!item) return NULL;
    return item->translated;
}

char const *t(char const *orig)
{
    char const *n = GetTranslatedString(orig);
    if (n) {
        return n;
    }
    return orig;
}

int
DoTranslate(ParsePtr p)
{
    int r;
    DynamicBuffer orig, translated;
    DBufInit(&orig);
    DBufInit(&translated);
    int c;

    c = ParseNonSpaceChar(p, &r, 1);
    if (r) return r;
    if (c == 0) {
        return E_EOLN;
    }

    if (c != '"') {
        r = ParseToken(p, &orig);
        if (r) return r;
        r = VerifyEoln(p);
        if (!StrCmpi(DBufValue(&orig), "dump")) {
            DBufFree(&orig);
            if (r) return r;
            DumpTranslationTable(stdout, 0);
            return OK;
        }
        if (!StrCmpi(DBufValue(&orig), "clear")) {
            DBufFree(&orig);
            if (r) return r;
            ClearTranslationTable();
            return OK;
        }
        return E_PARSE_ERR;
    }

    if ( (r=ParseQuotedString(p, &orig)) ) {
        return r;
    }
    if ( (r=ParseQuotedString(p, &translated)) ) {
        if (r == E_EOLN) {
            XlateItem *item = FindTranslation(DBufValue(&orig));
            if (item) {
                RemoveTranslation(item);
            }
            if (!strcmp(DBufValue(&orig), "LANGID")) {
                InsertTranslation("LANGID", "en");
            }
            r = OK;
        }
        DBufFree(&orig);
        return r;
    }

    if ( (r=VerifyEoln(p)) ) {
        DBufFree(&orig);
        DBufFree(&translated);
        return r;
    }
    r = InsertTranslation(DBufValue(&orig), DBufValue(&translated));
    DBufFree(&orig);
    DBufFree(&translated);
    return r;
}

void
get_translation_hash_stats(int *total, int *maxlen, double *avglen)
{
    struct hash_table_stats s;
    hash_table_get_stats(&TranslationTable, &s);
    *total = s.num_entries;
    *maxlen = s.max_len;
    *avglen = s.avg_len;
}

