/***************************************************************/
/*                                                             */
/*  USERFNS.C                                                  */
/*                                                             */
/*  This file contains the routines to support user-defined    */
/*  functions.                                                 */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stddef.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#include <string.h>

#include <stdlib.h>
#include "types.h"
#include "globals.h"
#include "protos.h"
#include "err.h"

/* The hash table */
hash_table FuncHash;

static void DestroyUserFunc (UserFunc *f);
static void FUnset (char const *name);
static void FSet (UserFunc *f);
static void RenameUserFunc(char const *oldname, char const *newname);

static unsigned int HashUserFunc(void *x)
{
    UserFunc *f = (UserFunc *) x;
    return HashVal_preservecase(f->name);
}

static int CompareUserFuncs(void *a, void *b)
{
    UserFunc *f = (UserFunc *) a;
    UserFunc *g = (UserFunc *) b;
    return strcmp(f->name, g->name);
}

void
InitUserFunctions(void)
{
    if (hash_table_init(&FuncHash,
                        offsetof(UserFunc, link),
                        HashUserFunc,
                        CompareUserFuncs) < 0) {
        fprintf(ErrFp, "Unable to initialize function hash table: Out of memory.  Exiting.\n");
        exit(1);
    }
}

/***************************************************************/
/*                                                             */
/*  HashVal_preservecase                                       */
/*  Given a string, compute the hash value.                    */
/*                                                             */
/***************************************************************/
unsigned int HashVal_preservecase(char const *str)
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
/*  DoFrename                                                  */
/*                                                             */
/*  Rename a user-defined function - the FRENAME command.      */
/*                                                             */
/***************************************************************/
int DoFrename(ParsePtr p)
{
    int r;
    DynamicBuffer oldbuf;
    DynamicBuffer newbuf;
    DBufInit(&oldbuf);
    DBufInit(&newbuf);

    r = ParseIdentifier(p, &oldbuf);
    if (r != OK) {
        DBufFree(&oldbuf);
        return r;
    }
    strtolower(DBufValue(&oldbuf));
    r = ParseIdentifier(p, &newbuf);
    if (r != OK) {
        DBufFree(&oldbuf);
        DBufFree(&newbuf);
        return r;
    }
    strtolower(DBufValue(&newbuf));
    r = VerifyEoln(p);
    if (r != OK) {
        DBufFree(&oldbuf);
        DBufFree(&newbuf);
        return r;
    }
    if (FindBuiltinFunc(DBufValue(&newbuf))) {
        Eprint("%s: `%s'", GetErr(E_REDEF_FUNC), DBufValue(&newbuf));
        DBufFree(&oldbuf);
        DBufFree(&newbuf);
        return E_REDEF_FUNC;
    }
    if (FindBuiltinFunc(DBufValue(&oldbuf))) {
        Eprint("%s: `%s'", GetErr(E_REDEF_FUNC), DBufValue(&oldbuf));
        DBufFree(&oldbuf);
        DBufFree(&newbuf);
        return E_REDEF_FUNC;
    }
    RenameUserFunc(DBufValue(&oldbuf), DBufValue(&newbuf));
    DBufFree(&oldbuf);
    DBufFree(&newbuf);
    return OK;
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
        Wprint(tr("Function `%s' redefined: previously defined at %s(%s)"),
               existing->name, existing->filename, line_range(existing->lineno_start, existing->lineno));
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
    func->lineno_start = LineNoStart;
    func->recurse_flag = 0;
    StrnCpy(func->name, DBufValue(&buf), VAR_NAME_LEN);
    DBufFree(&buf);
    if (!Hush) {
        if (FindBuiltinFunc(func->name)) {
            Eprint("%s: `%s'", GetErr(E_REDEF_FUNC), func->name);
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
            local_array[i].link.next = &(local_array[i+1]);
            local_array[i+1].link.next = NULL;
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
        Eprint("%s", GetErr(E_CANTNEST_FDEF));
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
        Wprint(tr("Warning: Function name `%s...' truncated to `%s'"),
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
    UserFunc *f = FindUserFunc(name);
    if (f) {
        hash_table_delete(&FuncHash, f);
        DestroyUserFunc(f);
    }
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
    hash_table_insert(&FuncHash, f);
}

UserFunc *FindUserFunc(char const *name)
{
   UserFunc *f;
   UserFunc candidate;

   StrnCpy(candidate.name, name, VAR_NAME_LEN);

   f = hash_table_find(&FuncHash, &candidate);
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

    f = hash_table_next(&FuncHash, NULL);
    while(f) {
        next = hash_table_next(&FuncHash, f);
        hash_table_delete_no_resize(&FuncHash, f);
        DestroyUserFunc(f);
        f = next;
    }
    hash_table_free(&FuncHash);
    InitUserFunctions();
}

/***************************************************************/
/*                                                             */
/*  RenameUserFunc                                             */
/*                                                             */
/*  Rename a user-defined function.                            */
/*  If newname exists, it is deleted.                          */
/*  If oldname exists, it is renamed to newname.               */
/*                                                             */
/***************************************************************/
static void
RenameUserFunc(char const *oldname, char const *newname)
{
    UserFunc *f = FindUserFunc(oldname);

    if (!strcmp(oldname, newname)) {
        /* Same name; do nothing */
        return;
    }

    /* Unset newname */
    FUnset(newname);

    /* If oldname does not exist, we're done. */
    if (!f) {
        return;
    }

    /* Remove from hash table */
    hash_table_delete(&FuncHash, f);

    /* Rename */
    StrnCpy(f->name, newname, VAR_NAME_LEN);

    /* Insert into hash table */
    hash_table_insert(&FuncHash, f);
}

void
dump_userfunc_hash_stats(void)
{
    hash_table_dump_stats(&FuncHash, ErrFp);
}

