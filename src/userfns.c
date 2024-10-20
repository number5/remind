/***************************************************************/
/*                                                             */
/*  USERFNS.C                                                  */
/*                                                             */
/*  This file contains the routines to support user-defined    */
/*  functions.                                                 */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <ctype.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <string.h>

#include <stdlib.h>
#include "types.h"
#include "globals.h"
#include "protos.h"
#include "err.h"

#define FUNC_HASH_SIZE 31   /* Size of User-defined function hash table */

/* The hash table */
static UserFunc *FuncHash[FUNC_HASH_SIZE];

static void DestroyUserFunc (UserFunc *f);
static void FUnset (char const *name);
static void FSet (UserFunc *f);

/***************************************************************/
/*                                                             */
/*  HashVal                                                    */
/*  Given a string, compute the hash value.                    */
/*                                                             */
/***************************************************************/
unsigned int HashVal_nocase(char const *str)
{
    unsigned int h = 0, high;
    while(*str) {
        h = (h << 4) + (unsigned int) *str;
        str++;
        high = h & 0xF0000000;
        if (high) {
            h ^= (high >> 24);
        }
        h &= ~high;
    }
    return h;
}

/***************************************************************/
/*                                                             */
/*  DoFunset                                                   */
/*                                                             */
/*  Undefine a user-defined function - the FUNSET command.     */
/*                                                             */
/***************************************************************/
int DoFunset(ParsePtr p)
{
    int r;
    int seen_one = 0;
    DynamicBuffer buf;
    DBufInit(&buf);

    while(1) {
        r = ParseIdentifier(p, &buf);
        if (r == E_EOLN) {
            DBufFree(&buf);
            break;
        }
        seen_one = 1;
        strtolower(DBufValue(&buf));
        FUnset(DBufValue(&buf));
        DBufFree(&buf);
    }
    if (seen_one) return OK;
    return E_PARSE_ERR;
}

/***************************************************************/
/*                                                             */
/*  DoFset                                                     */
/*                                                             */
/*  Define a user-defined function - the FSET command.         */
/*                                                             */
/***************************************************************/
int DoFset(ParsePtr p)
{
    int r;
    int c;
    int i;
    UserFunc *func;
    UserFunc *existing;
    Var *locals = NULL;
    Var local_array[MAX_FUNC_ARGS];
    int orig_namelen;

    DynamicBuffer buf;
    DBufInit(&buf);

    /* Get the function name */
    if ( (r=ParseIdentifier(p, &buf)) ) return r;
    if (*DBufValue(&buf) == '$') {
	DBufFree(&buf);
	return E_BAD_ID;
    }
    orig_namelen = buf.len;

    /* Convert to lower-case */
    strtolower(DBufValue(&buf));

    /* If the function exists and was defined at the same line of the same
       file, do nothing */
    existing = FindUserFunc(DBufValue(&buf));
    if (existing) {
        if (!strcmp(existing->filename, FileName) &&
            strcmp(existing->filename, "[cmdline]") &&
            existing->lineno == LineNo) {
            DBufFree(&buf);
            /* We already have it!  Our work here is done. */
            return OK;
        }
        /* Warn about redefinition */
        Wprint("Function %s redefined (previously defined at %s:%d)",
               existing->name, existing->filename, existing->lineno);
    }

    /* Should be followed by '(' */
    c = ParseNonSpaceChar(p, &r, 0);
    if (r) {
	DBufFree(&buf);
	return r;
    }
    if (c != '(') {
	DBufFree(&buf);
	return E_PARSE_ERR;
    }

    func = NEW(UserFunc);
    if (!func) {
	DBufFree(&buf);
	return E_NO_MEM;
    }
    if (FileName) {
        func->filename = StrDup(FileName);
    } else {
        func->filename = StrDup("[cmdline]");
    }
    if (!func->filename) {
        free(func);
        return E_NO_MEM;
    }
    func->lineno = LineNo;
    func->recurse_flag = 0;
    StrnCpy(func->name, DBufValue(&buf), VAR_NAME_LEN);
    DBufFree(&buf);
    if (!Hush) {
	if (FindBuiltinFunc(func->name)) {
	    Eprint("%s: `%s'", ErrMsg[E_REDEF_FUNC], func->name);
	}
    }
    func->node = NULL;
    func->nargs = 0;
    func->args = NULL;

    /* Get the local variables */

    c=ParseNonSpaceChar(p, &r, 1);
    if (r) return r;
    if (c == ')') {
	(void) ParseNonSpaceChar(p, &r, 0);
    } else {
        locals = local_array;
	while(1) {
	    if ( (r=ParseIdentifier(p, &buf)) ) return r;
	    if (*DBufValue(&buf) == '$') {
		DBufFree(&buf);
		DestroyUserFunc(func);
		return E_BAD_ID;
	    }
            /* If we've already seen this local variable, error */
            for (i=0; i<func->nargs; i++) {
                if (!StrinCmp(DBufValue(&buf), local_array[i].name, VAR_NAME_LEN)) {
                    DBufFree(&buf);
                    DestroyUserFunc(func);
                    return E_REPEATED_ARG;
                }
            }
            i = func->nargs;
            if (i >= MAX_FUNC_ARGS-1) {
                DBufFree(&buf);
                DestroyUserFunc(func);
                return E_2MANY_ARGS;
            }
            local_array[i].v.type = ERR_TYPE;
            StrnCpy(local_array[i].name, DBufValue(&buf), VAR_NAME_LEN);
            local_array[i].next = &(local_array[i+1]);
            local_array[i+1].next = NULL;
	    func->nargs++;
	    c = ParseNonSpaceChar(p, &r, 0);
            if (r) {
                DBufFree(&buf);
                DestroyUserFunc(func);
                return r;
            }
	    if (c == ')') break;
	    else if (c != ',') {
		DestroyUserFunc(func);
		return E_PARSE_ERR;
	    }
	}
    }

    /* Allow an optional = sign: FSET f(x) = x*x */
    c = ParseNonSpaceChar(p, &r, 1);
    if (r) {
        DestroyUserFunc(func);
        return r;
    }
    if (c == '=') {
	(void) ParseNonSpaceChar(p, &r, 0);
    }
    if (p->isnested) {
	Eprint("%s", ErrMsg[E_CANTNEST_FDEF]);
	DestroyUserFunc(func);
	return E_PARSE_ERR;
    }

    while(*(p->pos) && isspace(*(p->pos))) {
        p->pos++;
    }
    if (!*(p->pos)) {
        DestroyUserFunc(func);
        return E_EOLN;
    }
    /* Parse the expression */
    func->node = parse_expression(&(p->pos), &r, locals);
    if (!func->node) {
        DestroyUserFunc(func);
        return r;
    }

    c = ParseNonSpaceChar(p, &r, 1);
    if (c != 0 || r != 0) {
        DestroyUserFunc(func);
        if (r != 0) return r;
        return E_EXPECTING_EOL;
    }

    /* Save the argument names */
    if (func->nargs) {
        func->args = calloc(func->nargs, sizeof(char *));
        for (i=0; i<func->nargs; i++) {
            func->args[i] = StrDup(local_array[i].name);
            if (!func->args[i]) {
                DestroyUserFunc(func);
                return E_NO_MEM;
            }
        }
    }

    /* If an old definition of this function exists, destroy it */
    FUnset(func->name);

    /* Add the function definition */
    FSet(func);
    if (orig_namelen > VAR_NAME_LEN) {
	Wprint("Warning: Function name `%s...' truncated to `%s'",
	       func->name, func->name);
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  DestroyUserFunc                                            */
/*                                                             */
/*  Free up all the resources used by a user-defined function. */
/*                                                             */
/***************************************************************/
static void DestroyUserFunc(UserFunc *f)
{
    int i;

    /* Free the function definition */
    if (f->node) free_expr_tree(f->node);

    /* Free the filename */
    if (f->filename) free( (char *) f->filename);

    /* Free arg names */
    if (f->args) {
        for (i=0; i<f->nargs; i++) {
            if (f->args[i]) free(f->args[i]);
        }
        free(f->args);
    }

    /* Free the data structure itself */
    free(f);
}

/***************************************************************/
/*                                                             */
/*  FUnset                                                     */
/*                                                             */
/*  Delete the function definition with the given name, if     */
/*  it exists.                                                 */
/*                                                             */
/***************************************************************/
static void FUnset(char const *name)
{
    UserFunc *cur, *prev;
    int h;

    h = HashVal_nocase(name) % FUNC_HASH_SIZE;

    cur = FuncHash[h];
    prev = NULL;
    while(cur) {
	if (! strncmp(name, cur->name, VAR_NAME_LEN)) break;
	prev = cur;
	cur = cur->next;
    }
    if (!cur) return;
    if (prev) prev->next = cur->next; else FuncHash[h] = cur->next;
    DestroyUserFunc(cur);
}

/***************************************************************/
/*                                                             */
/*  FSet                                                       */
/*                                                             */
/*  Insert a user-defined function into the hash table.        */
/*                                                             */
/***************************************************************/
static void FSet(UserFunc *f)
{
    int h = HashVal_nocase(f->name) % FUNC_HASH_SIZE;
    f->next = FuncHash[h];
    FuncHash[h] = f;
}

UserFunc *FindUserFunc(char const *name)
{
   UserFunc *f;
   int h = HashVal_nocase(name) % FUNC_HASH_SIZE;

   /* Search for the function */
   f = FuncHash[h];
   while (f && strncmp(name, f->name, VAR_NAME_LEN)) f = f->next;
   return f;
}

/***************************************************************/
/*                                                             */
/*  UserFuncExists                                             */
/*                                                             */
/*  Return the number of arguments accepted by the function if */
/*  it is defined, or -1 if it is not defined.                 */
/*                                                             */
/***************************************************************/
int UserFuncExists(char const *fn)
{
    UserFunc *f = FindUserFunc(fn);

    if (!f) return -1;
    else return f->nargs;
}

/***************************************************************/
/*                                                             */
/*  UnsetAllUserFuncs                                          */
/*                                                             */
/*  Call FUNSET on all user funcs.  Used with -ds flag to      */
/*  ensure no expr_node memory leaks.                          */
/*                                                             */
/***************************************************************/
void
UnsetAllUserFuncs(void)
{
    UserFunc *f;
    UserFunc *next;
    int i;
    for (i=0; i<FUNC_HASH_SIZE; i++) {
        f = FuncHash[i];
        while(f) {
            next = f->next;
            DestroyUserFunc(f);
            f = next;
        }
        FuncHash[i] = NULL;
    }
}

void
get_userfunc_hash_stats(int *total, int *maxlen, double *avglen)
{
    int len;
    int i;
    UserFunc *f;

    *maxlen = 0;
    *total = 0;

    for (i=0; i<FUNC_HASH_SIZE; i++) {
        len = 0;
        f = FuncHash[i];
        while(f) {
            len++;
            (*total)++;
            f = f->next;
        }
        if (len > *maxlen) {
            *maxlen = len;
        }
    }
    *avglen = (double) *total / (double) FUNC_HASH_SIZE;
}
