/***************************************************************/
/*                                                             */
/*  UTILS.C                                                    */
/*                                                             */
/*  Useful utility functions.                                  */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

static char const DontEscapeMe[] =
"1234567890_-=+abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@.,/";

#include "config.h"
#include "err.h"

#include <string.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <stdio.h>
#include <ctype.h>

#include <stdlib.h>
#include "types.h"
#include "globals.h"
#include "protos.h"

/***************************************************************/
/*                                                             */
/*  StrnCpy                                                    */
/*                                                             */
/*  Just like strncpy EXCEPT we ALWAYS copy the trailing 0.    */
/*                                                             */
/***************************************************************/
char *StrnCpy(char *dest, char const *source, int n)
{
    char *odest = dest;

    if (n <= 0) {
        *dest = 0;
        return dest;
    }

    while (n-- && (*dest++ = *source++)) ;
    if (*(dest-1)) *dest = 0;
    return odest;
}

#ifndef HAVE_STRNCASECMP
/***************************************************************/
/*                                                             */
/*  StrinCmp - compare strings, case-insensitive               */
/*                                                             */
/***************************************************************/
int StrinCmp(char const *s1, char const *s2, int n)
{
    register int r;
    while (n && *s1 && *s2) {
        n--;
        r = toupper(*s1) - toupper(*s2);
        if (r) return r;
        s1++;
        s2++;
    }
    if (n) return (toupper(*s1) - toupper(*s2)); else return 0;
}

#endif

#ifndef HAVE_STRDUP
/***************************************************************/
/*                                                             */
/*  StrDup                                                     */
/*                                                             */
/*  Like ANSI strdup                                           */
/*                                                             */
/***************************************************************/
char *StrDup(char const *s)
{
    char *ret = malloc(strlen(s)+1);
    if (!ret) return NULL;
    strcpy(ret, s);
    return ret;
}

#endif

#ifndef HAVE_STRCASECMP
/***************************************************************/
/*                                                             */
/*  StrCmpi                                                    */
/*                                                             */
/*  Compare strings, case insensitive.                         */
/*                                                             */
/***************************************************************/
int StrCmpi(char const *s1, char const *s2)
{
    int r;
    while (*s1 && *s2) {
        r = toupper(*s1) - toupper(*s2);
        if (r) return r;
        s1++;
        s2++;
    }
    return toupper(*s1) - toupper(*s2);
}

#endif

/***************************************************************/
/*                                                             */
/*  DateOK                                                     */
/*                                                             */
/*  Return 1 if the date is OK, 0 otherwise.                   */
/*                                                             */
/***************************************************************/
int DateOK(int y, int m, int d)
{
    if (d < 1                 ||
        m < 0                 ||
        y < BASE              ||
        m > 11                ||
        y > BASE + YR_RANGE   ||
        d > DaysInMonth(m, y) ) return 0;
    return 1;
}

void strtolower(char *s)
{
    while (*s) {
        *s = tolower(*s);
        s++;
    }
}

/* Functions designed to defeat gcc optimizer */

int _private_mul_overflow(int a, int b)
{
    double aa = (double) a;
    double bb = (double) b;

    if (aa*bb > (double) INT_MAX || aa*bb < (double) INT_MIN) {
        return 1;
    }
    return 0;
}

int _private_add_overflow(int a, int b)
{
    double aa = (double) a;
    double bb = (double) b;

    if (aa+bb < (double) INT_MIN) return 1;
    if (aa+bb > (double) INT_MAX) return 1;
    return 0;
}
int _private_sub_overflow(int a, int b)
{
    double aa = (double) a;
    double bb = (double) b;

    if (aa-bb < (double) INT_MIN) return 1;
    if (aa-bb > (double) INT_MAX) return 1;
    return 0;
}

int
ShellEscape(char const *in, DynamicBuffer *out)
{
    unsigned char const *i = (unsigned char const *) in;
    while(*i) {
        /* Don't escape chars with high bit set.  That will mangle UTF-8 */
        if (! (*i & 0x80) ) {
            if (!strchr(DontEscapeMe, *i)) {
                if (DBufPutc(out, '\\') != OK) return E_NO_MEM;
            }
        }
        if (DBufPutc(out, *i++) != OK) return E_NO_MEM;
    }
    return OK;
}

/* Call-stack for printing errors from user-defined functions */
typedef struct cs_s {
    struct cs_s *next;
    char const *filename;
    char const *func;
    int lineno;
    int lineno_start;
} cs;

static cs *callstack = NULL;
static cs *freecs = NULL;

static void
destroy_cs(cs *entry)
{
    entry->next = freecs;
    freecs = entry;
}


int
push_call(char const *filename, char const *func, int lineno, int lineno_start)
{
    cs *entry;
    if (freecs) {
        entry = freecs;
        freecs = freecs->next;
    } else {
        entry = NEW(cs);
        if (!entry) {
            return E_NO_MEM;
        }
    }
    entry->next = NULL;
    entry->filename = filename;
    entry->func = func;
    entry->lineno = lineno;
    entry->lineno_start = lineno_start;
    entry->next = callstack;
    callstack = entry;
    return OK;
}

void
clear_callstack(void)
{
    cs *entry = callstack;
    cs *next;
    while(entry) {
        next = entry->next;
        destroy_cs(entry);
        entry = next;
    }
    callstack = NULL;
}

static void
print_callstack_aux(FILE *fp, cs *entry)
{
    int i = 0;
    char const *in = tr("In");
    cs *prev = NULL;
    while(entry) {
        if (prev) {
            in = tr("Called from");
        }
        if (!prev || strcmp(prev->func, entry->func) || strcmp(prev->filename, entry->filename) || prev->lineno != entry->lineno) {
            if (prev) {
                fprintf(fp, "\n");
            }
            fprintf(fp, "    ");
            fprintf(fp, tr("%s(%s): [#%d] %s function `%s'"), entry->filename, line_range(entry->lineno_start, entry->lineno), i, in, entry->func);
        }
        prev = entry;
        entry = entry->next;
        i++;
        if (i > 10) {
            break;
        }
    }
    if (entry) {
        (void) fprintf(fp, "\n    [");
        (void) fprintf(fp, "%s", tr("remaining call frames omitted"));
        (void) fprintf(fp, "]");

    }
}

int
print_callstack(FILE *fp)
{
    print_callstack_aux(fp, callstack);
    if (callstack) return 1;
    return 0;
}

void
pop_call(void)
{
    cs *entry = callstack;
    if (entry) {
        callstack = entry->next;
        destroy_cs(entry);
    }
}

char const *
line_range(int lineno_start, int lineno)
{
    static char buf[128];
    if (lineno_start == lineno) {
        snprintf(buf, sizeof(buf), "%d", lineno);
    } else {
        snprintf(buf, sizeof(buf), "%d:%d", lineno_start, lineno);
    }
    return buf;
}
