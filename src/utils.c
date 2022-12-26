/***************************************************************/
/*                                                             */
/*  UTILS.C                                                    */
/*                                                             */
/*  Useful utility functions.                                  */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2022 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

static char const DontEscapeMe[] =
"1234567890_-=+abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ@.,/";

#include "config.h"
#include "err.h"

#include <string.h>
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

    while (n-- && (*dest++ = *source++)) ;
    if (*(dest-1)) *dest = 0;
    return odest;
}

/***************************************************************/
/*                                                             */
/*  StrMatch                                                   */
/*                                                             */
/*  Checks that two strings match (case-insensitive) to at     */
/*  least the specified number of characters, or the length    */
/*  of the first string, whichever is greater.                 */
/*                                                             */
/***************************************************************/
int StrMatch(char const *s1, char const *s2, int n)
{
    int l;
    if ((l = strlen(s1)) < n) return 0;
    return !StrinCmp(s1, s2, l);
}

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
    else return 1;
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
    while(*in) {
        if (!strchr(DontEscapeMe, *in)) {
            if (DBufPutc(out, '\\') != OK) return E_NO_MEM;
        }
        if (DBufPutc(out, *in++) != OK) return E_NO_MEM;
    }
    return OK;
}

/* Call-stack for printing errors from user-defined functions */
typedef struct cs_s {
    struct cs_s *next;
    char const *filename;
    char const *func;
    int lineno;
} cs;

static cs *callstack = NULL;

static void
destroy_cs(cs *entry)
{
    if (entry->filename) free( (void *) entry->filename);
    if (entry->func) free( (void *) entry->func);
    free( (void *) entry);
}


int
push_call(char const *filename, char const *func, int lineno)
{
    cs *entry = NEW(cs);
    if (!entry) {
        return E_NO_MEM;
    }
    entry->next = NULL;
    entry->filename = StrDup(filename);
    entry->func = StrDup(func);
    entry->lineno = lineno;
    if (!entry->filename || !entry->func) {
        destroy_cs(entry);
        return E_NO_MEM;
    }
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

int
have_callstack(void)
{
    if (callstack) return 1;
    return 0;
}

static void
print_callstack_aux(FILE *fp, cs *entry)
{
    if (entry) {
        print_callstack_aux(fp, entry->next);
        fprintf(fp, "\n");
        (void) fprintf(fp, "%s(%d): In function `%s'", entry->filename, entry->lineno, entry->func);
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
