/***************************************************************/
/*                                                             */
/*  TRANS.C                                                    */
/*                                                             */
/*  Functions to manage the translation table.  Implements     */
/*  the TRANSLATE keyword.                                     */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
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

static XlateItem *FindTranslation(char const *orig);
static int printf_formatters_are_safe(char const *orig, char const *translated);

void
TranslationTemplate(char const *in)
{
    if (!*in) {
        return;
    }
    if (!strcmp(in, "LANGID")) {
        return;
    }

    printf("TRANSLATE ");
    print_escaped_string_helper(stdout, in, 1, 0);
    if (FindTranslation(in)) {
        printf(" ");
        print_escaped_string_helper(stdout, tr(in), 1, 0);
        printf("\n");
    } else {
        printf(" \"\"\n");
    }
}

static void
GenerateTranslationTemplate(void)
{
    int i;

    printf("# Translation table template\n\n");

    printf("TRANSLATE \"LANGID\" ");
    print_escaped_string_helper(stdout, tr("LANGID"), 1, 0);
    printf("\n\n");

    printf("BANNER %s\n", DBufValue(&Banner));

    printf("\n# Weekday Names\n");
    for (i=0; i<7; i++) {
        printf("SET $%s ", DayName[i]);
        print_escaped_string_helper(stdout, tr(DayName[i]), 1, 0);
        printf("\n");
    }

    printf("\n# Month Names\n");
    for (i=0; i<12; i++) {
        printf("SET $%s ", MonthName[i]);
        print_escaped_string_helper(stdout, tr(MonthName[i]), 1, 0);
        printf("\n");
    }

    printf("\n# Other Translation-related System Variables\n");
    GenerateSysvarTranslationTemplates();

    printf("\n# Error Messages\n");
    for (i=0; i<NumErrs; i++) {
        TranslationTemplate(ErrMsg[i]);
    }

    printf("\n# Other Messages\n");
    for (i=0; translatables[i] != NULL; i++) {
        TranslationTemplate(translatables[i]);
    }
}

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
    print_escaped_string_helper(fp, s, 0, 0);
}

static void
print_escaped_string_json(FILE *fp, char const *s)
{
    print_escaped_string_helper(fp, s, 0, 1);
}

void
print_escaped_string_helper(FILE *fp, char const *s, int esc_for_remind, int json) {
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
            if ((*s > 0 && *s < 32) || *s == 0x7f) {
                if (json) {
                    fprintf(fp, "\\u%04x", (unsigned int) *s);
                } else {
                    fprintf(fp, "\\x%02x", (unsigned int) *s);
                }
            } else {
                if (esc_for_remind && *s == '[') {
                    fprintf(fp, "[\"[\"]");
                } else {
                    putc(*s, fp);
                }
            }
            break;
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
    char const *t;
    if (!json) {
        fprintf(fp, "# Translation table\n");
        /* Always to LANGID first */
        t = GetTranslatedString("LANGID");
        if (t) {
            fprintf(fp, "TRANSLATE \"LANGID\" ");
            print_escaped_string(fp, t);
            fprintf(fp, "\n");
        }
    } else {
        fprintf(fp, "{");
    }
    item = hash_table_next(&TranslationTable, NULL);
    while(item) {
        if (!json) {
            if (strcmp(item->orig, "LANGID")) {
                fprintf(fp, "TRANSLATE ");
                print_escaped_string(fp, item->orig);
                fprintf(fp, " ");
                print_escaped_string(fp, item->translated);
                fprintf(fp, "\n");
            }
        } else {
            if (done) {
                fprintf(fp, ",");
            }
            done=1;
            print_escaped_string_json(fp, item->orig);
            fprintf(fp, ":");
            print_escaped_string_json(fp, item->translated);
        }
        item = hash_table_next(&TranslationTable, item);
    }
    if (json) {
        fprintf(fp, "}");
    }
}

static unsigned int
HashXlateItem(void const *x)
{
    XlateItem const *item = (XlateItem const *) x;
    return HashVal_preservecase(item->orig);
}

static int
CompareXlateItems(void const *a, void const *b)
{
    XlateItem const *i = (XlateItem const *) a;
    XlateItem const *j = (XlateItem const *) b;
    return strcmp(i->orig, j->orig);
}

void
InitTranslationTable(void)
{
    if (hash_table_init(&TranslationTable, offsetof(XlateItem, link),
                        HashXlateItem, CompareXlateItems) < 0) {
        fprintf(ErrFp, "Unable to initialize translation hash table: Out of memory.  Exiting.\n");
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
    XlateItem *item;

    if (!printf_formatters_are_safe(orig, translated)) {
        Eprint(tr("Invalid translation: Both original and translated must have the same printf-style formatting sequences in the same order."));
        return E_PARSE_ERR;
    }
    item = FindTranslation(orig);
    if (item) {
        if (!strcmp(item->translated, translated)) {
            /* Translation is the same; do nothing */
            return OK;
        }
        RemoveTranslation(item);
    }

    if (strcmp(orig, "LANGID") && (!strcmp(orig, translated))) {
        return OK;
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
    XlateItem const *item = FindTranslation(orig);
    if (!item) return NULL;
    return item->translated;
}

int
GetTranslatedStringTryingVariants(char const *orig, DynamicBuffer *out)
{
    DynamicBuffer in;
    char const *s;
    int first_lower = 0;
    int has_upper = 0;

    DBufInit(&in);

    /* Try exact match first */
    s = GetTranslatedString(orig);
    if (s) {
        DBufPuts(out, s);
        return 1;
    }

    /* Classify orig */
    s = orig;
    while (*s) {
        if (isupper(*s)) {
            has_upper = 1;
            break;
        }
        s++;
    }

    if (islower(*orig)) {
        first_lower = 1;
    }

    if (has_upper) {
        /* Try all lower-case version */
        DBufPuts(&in, orig);
        strtolower(DBufValue(&in));
        s = GetTranslatedString(DBufValue(&in));
        if (s) {
            DBufPuts(out, s);
            strtolower(DBufValue(out));
            *(DBufValue(out)) = toupper(*DBufValue(out));
            DBufFree(&in);
            return 1;
        }
    }

    if (first_lower) {
        /* Try ucfirst version */
        DBufFree(&in);
        DBufPuts(&in, orig);
        strtolower(DBufValue(&in));
        *(DBufValue(&in)) = toupper(*(DBufValue(&in)));
        s = GetTranslatedString(DBufValue(&in));
        if (s) {
            DBufPuts(out, s);
            strtolower(DBufValue(out));
            DBufFree(&in);
            return 1;
        }
    }
    DBufFree(&in);
    return 0;
}

char const *tr(char const *orig)
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
        if (!StrCmpi(DBufValue(&orig), "generate")) {
            DBufFree(&orig);
            if (r) return r;
            GenerateTranslationTemplate();
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
dump_translation_hash_stats(void)
{
    hash_table_dump_stats(&TranslationTable, ErrFp);
}

static void
get_printf_escapes(char const *str, DynamicBuffer *out)
{
    char const *s = str;
    while(*s) {
        if (*s == '%' && *(s+1) != 0) {

            /* %% is safe and does not need to be replicated in translation */
            if (*(s+1) == '%') {
                s += 2;
                continue;
            }
            s++;
            DBufPutc(out, *s);
            while (*s && *(s+1) && strchr("#0- +'I%123456789.hlqLjzZt", *s)) {
                s++;
                DBufPutc(out, *s);
            }
        }
        s++;
    }
}

static int
printf_formatters_are_safe(char const *orig, char const *translated)
{
    DynamicBuffer origEscapes;
    DynamicBuffer translatedEscapes;
    int dangerous;

    DBufInit(&origEscapes);
    DBufInit(&translatedEscapes);

    get_printf_escapes(orig, &origEscapes);
    get_printf_escapes(translated, &translatedEscapes);

    dangerous = strcmp(DBufValue(&origEscapes), DBufValue(&translatedEscapes));

    if (dangerous) {
        return 0;
    }
    return 1;
}
