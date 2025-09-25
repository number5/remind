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

static unsigned int HashUserFunc(void const *x)
{
    UserFunc const *f = (UserFunc const *) x;
    return HashVal_preservecase(f->name);
}

static int CompareUserFuncs(void const *a, void const *b)
{
    UserFunc const *f = (UserFunc const *) a;
    UserFunc const *g = (UserFunc const *) b;
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
    int suppress_redefined_function_warning = 0;
    int ch;

    DynamicBuffer buf;
    DBufInit(&buf);


    ch = ParseNonSpaceChar(p, &r, 1);
    if (r) {
        return r;
    }
    if (ch == '-') {
        r = ParseToken(p, &buf);
        if (r) {
            return r;
        }
        if (strcmp(DBufValue(&buf), "-")) {
            DBufFree(&buf);
            return E_PARSE_ERR;
        }
        suppress_redefined_function_warning = 1;
        DBufFree(&buf);
    }

    /* Get the function name */
    if ( (r=ParseIdentifier(p, &buf)) ) {
        return r;
    }
    if (*DBufValue(&buf) == '$') {
        DBufFree(&buf);
        return E_BAD_ID;
    }
    orig_namelen = buf.len;

    /* Convert to lower-case */
    strtolower(DBufValue(&buf));

    /* If we're ignoring the line, just update is_constant flag if needed */
    if (should_ignore_line()) {
        if (in_constant_context()) {
            DBufFree(&buf);
            return OK;
        }
        existing = FindUserFunc(DBufValue(&buf));
        if (existing) {
            nonconst_debug(!existing->is_constant, tr("Potential function definition considered non-constant because of context"));
            existing->is_constant = 0;
        }
        DBufFree(&buf);
        return OK;
    }
    /* If the function exists and was defined at the same line of the same
       file, do nothing */
    existing = FindUserFunc(DBufValue(&buf));
    if (existing) {
        if (!strcmp(existing->filename, GetCurrentFilename()) &&
            strcmp(existing->filename, "[cmdline]") &&
            existing->lineno == LineNo) {
            DBufFree(&buf);
            /* We already have it!  Our work here is done. */
            return OK;
        }
        /* Warn about redefinition */
        if (!suppress_redefined_function_warning && !existing->been_pushed) {
            if (warning_level("05.00.03")) {
                Wprint(tr("Function `%s' redefined: previously defined at %s(%s)"),
                       existing->name, existing->filename, line_range(existing->lineno_start, existing->lineno));
            }
        }
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
    if (GetCurrentFilename()) {
        func->filename = GetCurrentFilename();
    } else {
        func->filename = "[cmdline]";
    }
    func->lineno = LineNo;
    func->lineno_start = LineNoStart;
    func->recurse_flag = 0;
    func->been_pushed = 0;
    if (RunDisabled) {
        func->run_disabled = 1;
    } else {
        func->run_disabled = 0;
    }
    if (in_constant_context()) {
        func->is_constant = 1;
    } else {
        nonconst_debug(0, tr("Function definition considered non-constant because of context"));
        func->is_constant = 0;
    }
    StrnCpy(func->name, DBufValue(&buf), VAR_NAME_LEN);
    DBufFree(&buf);
    if (!Hush) {
        if (FindBuiltinFunc(func->name)) {
            if (warning_level("03.00.04")) {
                Eprint("%s: `%s'", GetErr(E_REDEF_FUNC), func->name);
            }
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
        if (!func->args) {
            DestroyUserFunc(func);
            return E_NO_MEM;
        }
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

typedef struct pushed_userfuncs {
    struct pushed_userfuncs *next;
    char const *filename;
    int lineno;
    int num_funcs;
    int alloc_funcs;
    UserFunc **funcs;
} PushedUserFuncs;

static PushedUserFuncs *UserFuncStack = NULL;
static UserFunc *clone_userfunc(char const *name, int *r)
{
    int i;
    UserFunc *src;
    UserFunc *dest = NEW(UserFunc);
    *r = E_NO_MEM;
    if (!dest) {
        return NULL;
    }

    /* Find the source function */
    src = FindUserFunc(name);

    /* If it doesn't exist, use sentinal values to indicate that */
    if (!src) {
        *r = OK;
        StrnCpy(dest->name, name, VAR_NAME_LEN);
        dest->is_constant = 0;
        dest->node = NULL;
        dest->args = NULL;
        dest->nargs = -1;
        dest->filename = NULL;
        dest->lineno = -1;
        dest->lineno_start = -1;
        dest->recurse_flag = 0;
        dest->been_pushed = 0;
        return dest;
    }
    /* Copy the whole thing; then adjust */
    *dest = *src;

    /* Allow warning-free redefinition of original function */
    src->been_pushed = 1;

    /* For safety */
    dest->node = NULL;
    dest->args = NULL;

    /* Copy args */
    if (dest->nargs) {
        dest->args = calloc(dest->nargs, sizeof(char *));
        if (!dest->args) {
            DestroyUserFunc(dest);
            return NULL;
        }
        for (i=0; i<dest->nargs; i++) {
            dest->args[i] = StrDup(src->args[i]);
            if (!dest->args[i]) {
                DestroyUserFunc(dest);
                return NULL;
            }
        }
    }

    /* Copy expr */
    dest->node = clone_expr_tree(src->node, r);
    if (!dest->node) {
        DestroyUserFunc(dest);
        return NULL;
    }
    *r = OK;
    return dest;
}

static int add_func_to_push(char const *name, PushedUserFuncs *pf)
{
    int r;
    UserFunc *clone = clone_userfunc(name, &r);
    if (!clone) {
        return r;
    }
    if (!pf->alloc_funcs) {
        pf->funcs = calloc(4, sizeof(UserFunc *));
        if (!pf->funcs) {
            DestroyUserFunc(clone);
            return E_NO_MEM;
        }
        pf->alloc_funcs = 4;
    } else {
        if (pf->num_funcs == pf->alloc_funcs) {
            pf->funcs = realloc(pf->funcs, 2 * pf->alloc_funcs * sizeof(UserFunc *));
            if (!pf->funcs) {
                DestroyUserFunc(clone);
                return E_NO_MEM;
            }
            pf->alloc_funcs *= 2;
        }
    }
    pf->funcs[pf->num_funcs] = clone;
    pf->num_funcs++;
    return OK;
}

static void free_pushed_funcs(PushedUserFuncs *pf)
{
    int i;
    if (pf->funcs) {
        for(i=0; i<pf->num_funcs; i++) {
            if (pf->funcs[i]) {
                DestroyUserFunc(pf->funcs[i]);
            }
        }
        free(pf->funcs);
    }
    free(pf);
}

int PushUserFuncs(ParsePtr p)
{
    int r;
    DynamicBuffer buf;
    char const *name;

    DBufInit(&buf);
    PushedUserFuncs *pf = NEW(PushedUserFuncs);
    if (!pf) {
        return E_NO_MEM;
    }
    pf->next = NULL;
    pf->funcs = NULL;
    pf->filename = GetCurrentFilename();
    pf->lineno = LineNo;
    pf->num_funcs = 0;
    pf->alloc_funcs = 0;

    while(1) {
        r = ParseIdentifier(p, &buf);
        if (r == E_EOLN) {
            break;
        }
        if (r) {
            DBufFree(&buf);
            free_pushed_funcs(pf);
            return r;
        }
        name = DBufValue(&buf);
        if (*name == '$') {
            DBufFree(&buf);
            free_pushed_funcs(pf);
            return E_BAD_ID;
        }

        r = add_func_to_push(name, pf);

        DBufFree(&buf);
        if (r != OK) {
            free_pushed_funcs(pf);
            return r;
        }
    }
    if (pf->num_funcs == 0) {
        free_pushed_funcs(pf);
        return E_EOLN;
    }
    pf->next = UserFuncStack;
    UserFuncStack = pf;
    return OK;
}

int PopUserFuncs(ParsePtr p)
{
    int i;
    PushedUserFuncs *pf = UserFuncStack;
    int r = VerifyEoln(p);
    if (r != OK) {
        return r;
    }
    if (!pf) {
        return E_POPF_NO_PUSH;
    }
    UserFuncStack = UserFuncStack->next;
    if (strcmp(pf->filename, GetCurrentFilename())) {
        Wprint(tr("POP-FUNCS at %s:%d matches PUSH-FUNCS in different file: %s:%d"), GetCurrentFilename(), LineNo, pf->filename, pf->lineno);
    }
    for (i=0; i<pf->num_funcs; i++) {
        UserFunc *clone = pf->funcs[i];
        FUnset(clone->name);
        if (clone->nargs < 0 && !clone->node) {
            /* Popping a function that should simply be unset... we are done */
            continue;
        }

        /* Insert the clone into the hash table */
        FSet(clone);

        /* Make sure we don't free the clone! */
        pf->funcs[i] = NULL;
    }
    free_pushed_funcs(pf);
    return OK;
}

int EmptyUserFuncStack(int print_unmatched)
{
    int j = 0;
    PushedUserFuncs *next;
    while(UserFuncStack) {
        if (print_unmatched) {
            Wprint(tr("Unmatched PUSH-FUNCS at %s(%d)"),
                   UserFuncStack->filename,
                   UserFuncStack->lineno);
        }
        j++;
        next = UserFuncStack->next;
        free_pushed_funcs(UserFuncStack);
        UserFuncStack = next;
    }
    return j;
}

