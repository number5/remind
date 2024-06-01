/***************************************************************/
/*                                                             */
/*  EXPR.C                                                     */
/*                                                             */
/*  Remind's expression-evaluation engine                      */
/*                                                             */
/*  This engine breaks expression evaluation into two phases:  */
/*  1) Compilation: The expression is parsed and a tree        */
/*     structure consisting of linked expr_node obbjects       */
/*     is created.                                             */
/*  2) Evaluation: The expr_node tree is traversed and         */
/*     evaluated.                                              */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/
#include "config.h"
#include "err.h"
#include "types.h"
#include "protos.h"
#include "globals.h"
#include "expr.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/*
  The parser parses expressions into an internal tree
  representation.  Each node in the tree is an expr_node
  object, which can be one of the following types:
  0) N_FREE:         An unallocated node
  1) N_CONSTANT:     A constant, such as 3, 13:30, '2024-01-01' or "foo"
  2) N_LOCAL_VAR:    A reference to a function argument.
  3) N_VARIABLE:     A reference to a global variable
  4) N_SYSVAR:       A reference to a system variable
  5) N_BUILTIN_FUNC: A reference to a built-in function
  6) N_USER_FUNC:    A reference to a user-defined function
  7) N_OPERATOR:     A reference to an operator such as "+" or "&&"
  8) N_ERROR:        A node resulting from a parse error

  Additional types are N_SHORT_VAR, N_SHORT_SYSVAR, and N_SHORT_USER_FUNC
  which behave identically to N_VARIABLE, N_SYSVAR and N_USER_FUNC
  respectively, but have short-enough names to be stored more efficiently
  in the expr_node object.

  expr_nodes contain the following data, depending on their type:

  1) N_CONSTANT: The constant value is stored in the node's u.value field
  2) N_LOCAL_VAR: The offset into the functions argument list is stored in
     u.arg
  3) N_VARIABLE: The variable's name is stored in u.value.v.str
  4) N_SYSVAR: The system variable's name is stored in u.value.v.str
  5) N_BUILTIN_FUNC: A pointer to the function descriptor is stored in
     u.builtin_func
  6) N_USER_FUNC: The function's name is stored in u.value.v.str
  7) N_OPERATOR: A pointer to the operator function is stored in
     u.operator_func
  8) N_SHORT_VAR: The variable's name is stored in u.name
  9) N_SHORT_SYSVAR: The system variable's name is stored in u.name
  10) N_SHORT_USER_FUNC: The function's name is stored in u.name
 */

/* Constants for the "how" arg to compare() */
enum { EQ, GT, LT, GE, LE, NE };

/* Our pool of free expr_node objext, as a linked list, linked by child ptr */
static expr_node *expr_node_free_list = NULL;

#define TOKEN_IS(x) (!strcmp(DBufValue(&ExprBuf), x))
#define TOKEN_ISNOT(x) strcmp(DBufValue(&ExprBuf), x)
#define ISID(c) (isalnum(c) || (c) == '_')

/* Threshold above which to malloc space for function args rather
   than using the stack */
#define STACK_ARGS_MAX 5

/* Macro that only does "x" if the "-x" debug flag is on */
#define DBG(x) do { if (DebugFlag & DB_PRTEXPR) { x; } } while(0)

#define PEEK_TOKEN() peek_expr_token(&ExprBuf, *e)
#define GET_TOKEN() parse_expr_token(&ExprBuf, e)

/* The built-in function table lives in funcs.c */
extern BuiltinFunc Func[];
extern int NumFuncs;

/* Keep track of expr_node usage */
static int ExprNodesAllocated = 0;
static int ExprNodesHighWater = 0;
static int ExprNodesUsed = 0;

/* Forward references */
static expr_node * parse_expression_aux(char const **e, int *r, Var *locals);
static char const *get_operator_name(expr_node *node);

/* This is super-skanky... we keep track of the currently-executing
   user-defined function in a global var */
static UserFunc *CurrentUserFunc = NULL;

/* How many expr_node objects to allocate at a time */
#define ALLOC_CHUNK 64


/***************************************************************/
/*                                                             */
/* alloc_expr_node - allocate an expr_node object              */
/*                                                             */
/* Allocates and returns an expr_node object.  If all goes     */
/* well, a pointer to the object is returned and *r is set     */
/* to OK.  On failure, *r is set to an error code and NULL     */
/* is returned.                                                */
/*                                                             */
/***************************************************************/
static expr_node *
alloc_expr_node(int *r)
{
    expr_node *node;
    if (!expr_node_free_list) {
        expr_node_free_list = calloc(ALLOC_CHUNK, sizeof(expr_node));
        if (!expr_node_free_list) {
            *r = E_NO_MEM;
            return NULL;
        }
        ExprNodesAllocated += ALLOC_CHUNK;
        for (size_t i=0; i<ALLOC_CHUNK-1; i++) {
            expr_node_free_list[i].child = &(expr_node_free_list[i+1]);
            expr_node_free_list[i].sibling = NULL;
            expr_node_free_list[i].type = N_FREE;
        }
        expr_node_free_list[ALLOC_CHUNK-1].child = NULL;
    }
    ExprNodesUsed++;
    if (ExprNodesUsed > ExprNodesHighWater) ExprNodesHighWater = ExprNodesUsed;
    node = expr_node_free_list;
    expr_node_free_list = node->child;
    node->type = N_FREE;
    node->child = NULL;
    node->sibling = NULL;
    node->num_kids = 0;
    return node;
}

static void
add_child(expr_node *parent, expr_node *child)
{
    parent->num_kids++;
    child->sibling = NULL;
    if (!parent->child) {
        parent->child = child;
        return;
    }

    expr_node *cur = parent->child;
    while (cur->sibling) {
        cur = cur->sibling;
    }
    cur->sibling = child;
}

static void
debug_evaluation(Value *ans, int r, char const *fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);
    vfprintf(ErrFp, fmt, argptr);
    fprintf(ErrFp, " => ");
    if (r != OK) {
        fprintf(ErrFp, "%s\n", ErrMsg[r]);
    } else {
        PrintValue(ans, ErrFp);
        fprintf(ErrFp, "\n");
    }
    va_end(argptr);
}

static void
debug_evaluation_binop(Value *ans, int r, Value *v1, Value *v2, char const *fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);
    if (v1) {
        PrintValue(v1, ErrFp);
    } else {
        fprintf(ErrFp, "?");
    }
    fprintf(ErrFp, " ");
    vfprintf(ErrFp, fmt, argptr);
    fprintf(ErrFp, " ");
    if (v2) {
        PrintValue(v2, ErrFp);
    } else {
        fprintf(ErrFp, "?");
    }
    fprintf(ErrFp, " => ");
    if (r != OK) {
        fprintf(ErrFp, "%s\n", ErrMsg[r]);
    } else {
        PrintValue(ans, ErrFp);
        fprintf(ErrFp, "\n");
    }
    va_end(argptr);
}

static void
debug_evaluation_unop(Value *ans, int r, Value *v1, char const *fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);
    vfprintf(ErrFp, fmt, argptr);
    fprintf(ErrFp, " ");
    if (v1) {
        PrintValue(v1, ErrFp);
    } else {
        fprintf(ErrFp, "?");
    }
    fprintf(ErrFp, " => ");
    if (r != OK) {
        fprintf(ErrFp, "%s\n", ErrMsg[r]);
    } else {
        PrintValue(ans, ErrFp);
        fprintf(ErrFp, "\n");
    }
    va_end(argptr);
}

static int
get_var(expr_node *node, Value *ans)
{
    if (node->type == N_SHORT_VAR) {
        return GetVarValue(node->u.name, ans);
    } else {
        return GetVarValue(node->u.value.v.str, ans);
    }
}

static int
get_sysvar(expr_node *node, Value *ans)
{
    if (node->type == N_SHORT_SYSVAR) {
        return GetSysVar(node->u.name, ans);
    } else {
        return GetSysVar(node->u.value.v.str, ans);
    }
}

static int
eval_builtin(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    func_info info;
    BuiltinFunc *f = node->u.builtin_func;
    expr_node *kid;
    int i, j, r;
    Value stack_args[STACK_ARGS_MAX];

    /* Check that we have the right number of argumens */
    if (node->num_kids < f->minargs) return E_2FEW_ARGS;
    if (node->num_kids > f->maxargs && f->maxargs != NO_MAX) return E_2MANY_ARGS;
    if (f->newfunc) {
        return node->u.builtin_func->newfunc(node, locals, ans, nonconst);
    }

    /* Build up the old-style stack frame */
    info.nargs = node->num_kids;

    if (info.nargs) {
        if (info.nargs <= STACK_ARGS_MAX) {
            info.args = stack_args;
        } else {
            info.args = malloc(info.nargs * sizeof(Value));
            if (!info.args) {
                return E_NO_MEM;
            }
        }
    } else {
        info.args = NULL;
    }

    kid = node->child;
    i = 0;
    while (kid) {
        r = evaluate_expr_node(kid, locals, &(info.args[i]), nonconst);
        if (r != OK) {
            for (j=0; j<i; j++) {
                DestroyValue(info.args[j]);
            }
            if (info.args != NULL && info.args != stack_args) {
                free(info.args);
            }
            return r;
        }
        i++;
        kid = kid->sibling;
    }

    /* Actually call the function */
    if (DebugFlag & DB_PRTEXPR) {
        fprintf(ErrFp, "%s(", f->name);
        for (i=0; i<info.nargs; i++) {
            if (i > 0) {
                fprintf(ErrFp, " ");
            }
            PrintValue(&(info.args[i]), ErrFp);
            if (i < info.nargs-1) {
                fprintf(ErrFp, ",");
            }
        }
        fprintf(ErrFp, ") => ");
    }
    r = f->func(&info);
    if (r == OK) {
        r = CopyValue(ans, &info.retval);
    }

    /* Debug */
    if (DebugFlag & DB_PRTEXPR) {
        if (r) {
            fprintf(ErrFp, "%s", ErrMsg[r]);
        } else {
            PrintValue(ans, ErrFp);
        }
        fprintf(ErrFp, "\n");
    }
    if (r != OK) {
        Eprint("%s(): %s", f->name, ErrMsg[r]);
    }
    /* Clean up */
    if (info.args) {
        for (i=0; i<info.nargs; i++) {
            DestroyValue(info.args[i]);
        }
        if (info.args != NULL && info.args != stack_args) {
            free(info.args);
        }
    }
    DestroyValue(info.retval);
    return r;
}

static void
debug_enter_userfunc(expr_node *node, Value *locals, int nargs)
{
    char const *fname;
    int i;
    if (node->type == N_SHORT_USER_FUNC) {
        fname = node->u.name;
    } else {
        fname = node->u.value.v.str;
    }
    fprintf(ErrFp, "%s %s(", ErrMsg[E_ENTER_FUN],  fname);
    for (i=0; i<nargs; i++) {
        if (i) fprintf(ErrFp, ", ");
        PrintValue(&(locals[i]), ErrFp);
    }
    fprintf(ErrFp, ")\n");
}

static void
debug_exit_userfunc(expr_node *node, Value *ans, int r, Value *locals, int nargs)
{
    char const *fname;
    int i;
    if (node->type == N_SHORT_USER_FUNC) {
        fname = node->u.name;
    } else {
        fname = node->u.value.v.str;
    }
    fprintf(ErrFp, "%s %s(", ErrMsg[E_LEAVE_FUN], fname);
    for (i=0; i<nargs; i++) {
        if (i) fprintf(ErrFp, ", ");
        PrintValue(&(locals[i]), ErrFp);
    }
    fprintf(ErrFp, ") => ");
    if (r == OK) {
        PrintValue(ans, ErrFp);
    } else {
        fprintf(ErrFp, "%s", ErrMsg[r]);
    }
    fprintf(ErrFp, "\n");
}

static int
eval_userfunc(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    UserFunc *f;
    UserFunc *previously_executing;

    Value stack_locals[STACK_ARGS_MAX];

    char const *fname;
    if (node->type == N_SHORT_USER_FUNC) {
        fname = node->u.name;
    } else {
        fname = node->u.value.v.str;
    }
    f = FindUserFunc(fname);
    Value *new_locals = NULL;
    expr_node *kid;
    int i, r, j, pushed;

    if (!f) {
        Eprint("%s: `%s'", ErrMsg[E_UNDEF_FUNC], fname);
        return E_UNDEF_FUNC;
    }
    if (node->num_kids < f->nargs) {
        DBG(fprintf(ErrFp, "%s(...) => %s\n", fname, ErrMsg[E_2FEW_ARGS]));
        return E_2FEW_ARGS;
    }
    if (node->num_kids > f->nargs) {
        DBG(fprintf(ErrFp, "%s(...) => %s\n", fname, ErrMsg[E_2MANY_ARGS]));
        return E_2MANY_ARGS;
    }

    /* Build up the array of locals */
    if (node->num_kids) {
        if (node->num_kids > STACK_ARGS_MAX) {
            new_locals = malloc(node->num_kids * sizeof(Value));
        } else {
            new_locals = stack_locals;
        }
        if (!new_locals) {
            DBG(fprintf(ErrFp, "%s(...) => %s\n", fname, ErrMsg[E_NO_MEM]));
            return E_NO_MEM;
        }
        kid = node->child;
        i = 0;
        while(kid) {
            r = evaluate_expr_node(kid, locals, &(new_locals[i]), nonconst);
            if (r != OK) {
                for (j=0; j<i; j++) {
                    DestroyValue(new_locals[j]);
                }
                if (new_locals != stack_locals) free(new_locals);
                return r;
            }
            i++;
            kid = kid->sibling;
        }
    }

    if (FuncRecursionLevel >= MAX_RECURSION_LEVEL) {
        for (j=0; j<node->num_kids; j++) {
            DestroyValue(new_locals[j]);
        }
        if (new_locals != NULL && new_locals != stack_locals) free(new_locals);
        return E_RECURSIVE;
    }

    previously_executing = CurrentUserFunc;
    CurrentUserFunc = f;
    FuncRecursionLevel++;
    pushed = push_call(f->filename, f->name, f->lineno);
    if (DebugFlag & DB_PRTEXPR) {
        debug_enter_userfunc(node, new_locals, f->nargs);
    }

    r = evaluate_expr_node(f->node, new_locals, ans, nonconst);

    if (DebugFlag & DB_PRTEXPR) {
        debug_exit_userfunc(node, ans, r, new_locals, f->nargs);
    }
    if (r != OK) {
        Eprint("%s", ErrMsg[r]);
    }
    if (pushed == OK) pop_call();
    FuncRecursionLevel--;
    CurrentUserFunc = previously_executing;

    /* Clean up */
    for (j=0; j<node->num_kids; j++) {
        DestroyValue(new_locals[j]);
    }
    if (new_locals != stack_locals &&
        new_locals != NULL) {
        free(new_locals);
    }

    return r;
}

int
evaluate_expr_node(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;

    if (!node) {
        return E_SWERR;
    }
    switch(node->type) {
    case N_FREE:
    case N_ERROR:
        ans->type = ERR_TYPE;
        return E_SWERR;
    case N_CONSTANT:
        return CopyValue(ans, &(node->u.value));
    case N_SHORT_VAR:
        *nonconst = 1;
        r = get_var(node, ans);
        DBG(debug_evaluation(ans, r, "%s", node->u.name));
        return r;
    case N_VARIABLE:
        *nonconst = 1;
        r = get_var(node, ans);
        DBG(debug_evaluation(ans, r, "%s", node->u.value.v.str));
        return r;
    case N_LOCAL_VAR:
        r = CopyValue(ans, &(locals[node->u.arg]));
        DBG(debug_evaluation(ans, r, "%s", CurrentUserFunc->args[node->u.arg]));
        return r;
    case N_SHORT_SYSVAR:
        *nonconst = 1;
        r = get_sysvar(node, ans);
        DBG(debug_evaluation(ans, r, "$%s", node->u.name));
        return r;
    case N_SYSVAR:
        *nonconst = 1;
        r = get_sysvar(node, ans);
        DBG(debug_evaluation(ans, r, "$%s", node->u.value.v.str));
        return r;
    case N_BUILTIN_FUNC:
        if (!node->u.builtin_func->is_constant) {
            *nonconst = 1;
        }
        return eval_builtin(node, locals, ans, nonconst);
    case N_USER_FUNC:
    case N_SHORT_USER_FUNC:
        return eval_userfunc(node, locals, ans, nonconst);
    case N_OPERATOR:
        r = node->u.operator_func(node, locals, ans, nonconst);
        if (r != OK) {
            Eprint("`%s': %s", get_operator_name(node), ErrMsg[r]);
        }
        return r;
    }
    return E_SWERR;
}

static char const *how_to_op(int how)
{
    switch(how) {
    case EQ: return "==";
    case NE: return "!=";
    case GE: return ">=";
    case LE: return "<=";
    case GT: return ">";
    case LT: return "<";
    default: return "???";
    }
}

/*
 * All of the functions that implement operators
 */
static int
compare(expr_node *node, Value *locals, Value *ans, int *nonconst, int how)
{
    int r;
    Value v1, v2;
    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    r = evaluate_expr_node(node->child->sibling, locals, &v2, nonconst);
    if (r != OK) {
        DestroyValue(v1);
        return r;
    }

    ans->type = INT_TYPE;

    /* Different types? Only allowed for != and == */
    if (v1.type != v2.type) {
        if (how == EQ) {
            ans->v.val = 0;
        } else if (how == NE) {
            ans->v.val = 1;
        } else {
            DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "%s", how_to_op(how)));
            DestroyValue(v1);
            DestroyValue(v2);
            return E_BAD_TYPE;
        }
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "%s", how_to_op(how)));
        DestroyValue(v1);
        DestroyValue(v2);
        return OK;
    }

    /* Same types */
    if (v1.type == STR_TYPE) {
	switch(how) {
	case EQ: ans->v.val = (strcmp(v1.v.str, v2.v.str) == 0); break;
	case NE: ans->v.val = (strcmp(v1.v.str, v2.v.str) != 0); break;
	case LT: ans->v.val = (strcmp(v1.v.str, v2.v.str) < 0);  break;
	case GT: ans->v.val = (strcmp(v1.v.str, v2.v.str) > 0);  break;
	case LE: ans->v.val = (strcmp(v1.v.str, v2.v.str) <= 0); break;
	case GE: ans->v.val = (strcmp(v1.v.str, v2.v.str) >= 0); break;
	}
    } else {
	switch(how) {
	case EQ: ans->v.val = (v1.v.val == v2.v.val); break;
	case NE: ans->v.val = (v1.v.val != v2.v.val); break;
	case LT: ans->v.val = (v1.v.val < v2.v.val);  break;
	case GT: ans->v.val = (v1.v.val > v2.v.val);  break;
	case LE: ans->v.val = (v1.v.val <= v2.v.val); break;
	case GE: ans->v.val = (v1.v.val >= v2.v.val); break;
	}
    }
    DBG(debug_evaluation_binop(ans, r, &v1, &v2, "%s", how_to_op(how)));
    DestroyValue(v1);
    DestroyValue(v2);
    return OK;
}

static int compare_eq(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, EQ);
}
static int compare_ne(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, NE);
}
static int compare_le(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, LE);
}
static int compare_ge(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, GE);
}
static int compare_lt(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, LT);
}
static int compare_gt(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, GT);
}


static int
add(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1, v2;
    size_t l1, l2;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    r = evaluate_expr_node(node->child->sibling, locals, &v2, nonconst);
    if (r != OK) {
        DestroyValue(v1);
        return r;
    }

    /* If both are ints, just add 'em */
    if (v2.type == INT_TYPE && v1.type == INT_TYPE) {
        /* Check for overflow */
        if (_private_add_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "+"));
            return E_2HIGH;
        }
        *ans = v1;
        ans->v.val += v2.v.val;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "+"));
	return OK;
    }

/* If it's a date plus an int, add 'em */
    if ((v1.type == DATE_TYPE && v2.type == INT_TYPE) ||
	(v1.type == INT_TYPE && v2.type == DATE_TYPE)) {
        if (_private_add_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "+"));
            return E_DATE_OVER;
        }

        *ans = v1;
        ans->v.val += v2.v.val;
	if (ans->v.val < 0) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "+"));
            return E_DATE_OVER;
        }
	ans->type = DATE_TYPE;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "+"));
	return OK;
    }

/* If it's a datetime plus an int or a time, add 'em */
    if ((v1.type == DATETIME_TYPE && (v2.type == INT_TYPE || v2.type == TIME_TYPE)) ||
	((v1.type == INT_TYPE || v1.type == TIME_TYPE) && v2.type == DATETIME_TYPE)) {
        if (_private_add_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "+"));
            return E_DATE_OVER;
        }
        *ans = v1;
	ans->v.val += v2.v.val;
	if (ans->v.val < 0) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "+"));
            return E_DATE_OVER;
        }
	ans->type = DATETIME_TYPE;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "+"));
	return OK;
    }

/* If it's a time plus an int or a time plus a time,
   add 'em mod MINUTES_PER_DAY */
    if ((v1.type == TIME_TYPE && v2.type == INT_TYPE) ||
	(v1.type == INT_TYPE && v2.type == TIME_TYPE) ||
	(v1.type == TIME_TYPE && v2.type == TIME_TYPE)) {
        if (_private_add_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "+"));
            return E_DATE_OVER;
        }
        *ans = v1;
	ans->v.val += v2.v.val;
	ans->v.val = ans->v.val % MINUTES_PER_DAY;
	if (ans->v.val < 0) ans->v.val += MINUTES_PER_DAY;
	ans->type = TIME_TYPE;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "+"));
	return OK;
    }

/* If either is a string, coerce them both to strings and concatenate */
    if (v1.type == STR_TYPE || v2.type == STR_TYPE) {
        /* Skanky... copy the values shallowly fode debug */
        Value o1 = v1;
        Value o2 = v2;
	if ( (r = DoCoerce(STR_TYPE, &v1)) ) {
            DBG(debug_evaluation_binop(ans, r, &o1, &o2, "+"));
	    DestroyValue(v1);
            DestroyValue(v2);
	    return r;
	}
	if ( (r = DoCoerce(STR_TYPE, &v2)) ) {
            DBG(debug_evaluation_binop(ans, r, &o1, &o2, "+"));
	    DestroyValue(v1);
            DestroyValue(v2);
	    return r;
	}
	l1 = strlen(v1.v.str);
	l2 = strlen(v2.v.str);
	if (MaxStringLen > 0 && (l1 + l2 > (size_t) MaxStringLen)) {
            DBG(debug_evaluation_binop(ans, E_STRING_TOO_LONG, &o1, &o2, "+"));
	    DestroyValue(v1);
            DestroyValue(v2);
	    return E_STRING_TOO_LONG;
	}
	ans->v.str = malloc(l1 + l2 + 1);
	if (!ans->v.str) {
            DBG(debug_evaluation_binop(ans, E_NO_MEM, &o1, &o2, "+"));
	    DestroyValue(v1);
            DestroyValue(v2);
	    return E_NO_MEM;
	}
        ans->type = STR_TYPE;
	strcpy(ans->v.str, v1.v.str);
	strcpy(ans->v.str+l1, v2.v.str);
        DBG(debug_evaluation_binop(ans, OK, &o1, &o2, "+"));
	return OK;
    }

    /* Don't handle other types yet */
    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "+"));
    DestroyValue(v1);
    DestroyValue(v2);
    return E_BAD_TYPE;
}

static int
subtract(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1, v2;
    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    r = evaluate_expr_node(node->child->sibling, locals, &v2, nonconst);
    if (r != OK) {
        DestroyValue(v1);
        return r;
    }
    /* If they're both INTs, do subtraction */
    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "-"));
            return E_2HIGH;
        }
        *ans = v1;
	ans->v.val -= v2.v.val;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "-"));
	return OK;
    }

    /* If it's a date minus an int, do subtraction, checking for underflow */
    if (v1.type == DATE_TYPE && v2.type == INT_TYPE) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "-"));
            return E_DATE_OVER;
        }
	*ans = v1;
	ans->v.val -= v2.v.val;
	if (ans->v.val < 0) return E_DATE_OVER;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "-"));
	return OK;
    }

    /* If it's a datetime minus an int or a time, do subtraction,
     * checking for underflow */
    if (v1.type == DATETIME_TYPE && (v2.type == INT_TYPE || v2.type == TIME_TYPE)) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "-"));
            return E_DATE_OVER;
        }
        *ans = v1;
	ans->v.val -= v2.v.val;
	if (ans->v.val < 0) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "-"));
            return E_DATE_OVER;
        }
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "-"));
	return OK;
    }

    /* If it's a time minus an int, do subtraction mod MINUTES_PER_DAY */
    if (v1.type == TIME_TYPE && v2.type == INT_TYPE) {
        *ans = v1;
	ans->v.val = (ans->v.val - v2.v.val) % MINUTES_PER_DAY;
	if (ans->v.val < 0) ans->v.val += MINUTES_PER_DAY;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "-"));
	return OK;
    }

    /* If it's a time minus a time or a date minus a date, do it */
    if ((v1.type == TIME_TYPE && v2.type == TIME_TYPE) ||
	(v1.type == DATETIME_TYPE && v2.type == DATETIME_TYPE) ||
	(v1.type == DATE_TYPE && v2.type == DATE_TYPE)) {
        if (_private_sub_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_DATE_OVER, &v1, &v2, "-"));
            return E_DATE_OVER;
        }
        *ans = v1;
	ans->v.val -= v2.v.val;
	ans->type = INT_TYPE;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "-"));
	return OK;
    }

    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "-"));
    /* Must be types illegal for subtraction */
    DestroyValue(v1); DestroyValue(v2);
    return E_BAD_TYPE;
}

static int
multiply(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1, v2;
    char *ptr;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    r = evaluate_expr_node(node->child->sibling, locals, &v2, nonconst);
    if (r != OK) {
        DestroyValue(v1);
        return r;
    }

    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
        /* Prevent floating-point exception */
        if ((v2.v.val == -1 && v1.v.val == INT_MIN) ||
            (v1.v.val == -1 && v2.v.val == INT_MIN)) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "*"));
            return E_2HIGH;
        }
        if (_private_mul_overflow(v1.v.val, v2.v.val)) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "*"));
            return E_2HIGH;
        }
	*ans = v1;;
	ans->v.val *= v2.v.val;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "*"));
	return OK;
    }

    /* String times int means repeat the string that many times */
    if ((v1.type == INT_TYPE && v2.type == STR_TYPE) ||
        (v1.type == STR_TYPE && v2.type == INT_TYPE)) {
        int rep = (v1.type == INT_TYPE ? v1.v.val : v2.v.val);
        char const *str = (v1.type == INT_TYPE ? v2.v.str : v1.v.str);
        int l;

        /* Can't multiply by a negative number */
        if (rep < 0) {
            DBG(debug_evaluation_binop(ans, E_2LOW, &v1, &v2, "*"));
            DestroyValue(v1);
            DestroyValue(v2);
            return E_2LOW;
        }
        if (rep == 0 || !str || !*str) {
            /* Empty string */
            ans->type = STR_TYPE;
            ans->v.str = malloc(1);
            if (!ans->v.str) {
                DBG(debug_evaluation_binop(ans, E_NO_MEM, &v1, &v2, "*"));
                DestroyValue(v1);
                DestroyValue(v2);
                return E_NO_MEM;
            }
            *ans->v.str = 0;
            DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "*"));
            DestroyValue(v1); DestroyValue(v2);
            return OK;
        }

        /* Create the new value */
        l = (int) strlen(str);
        if (l * rep < 0) {
            DBG(debug_evaluation_binop(ans, E_STRING_TOO_LONG, &v1, &v2, "*"));
            DestroyValue(v1); DestroyValue(v2);
            return E_STRING_TOO_LONG;
        }
        if ((unsigned long) l * (unsigned long) rep >= (unsigned long) INT_MAX) {
            DBG(debug_evaluation_binop(ans, E_STRING_TOO_LONG, &v1, &v2, "*"));
            DestroyValue(v1); DestroyValue(v2);
            return E_STRING_TOO_LONG;
        }
        if (MaxStringLen > 0 && ((unsigned long) l * (unsigned long) rep) > (unsigned long)MaxStringLen) {
            DBG(debug_evaluation_binop(ans, E_STRING_TOO_LONG, &v1, &v2, "*"));
            DestroyValue(v1); DestroyValue(v2);
            return E_STRING_TOO_LONG;
        }
        ans->type = STR_TYPE;
        ans->v.str = malloc(l * rep + 1);
        if (!ans->v.str) {
            DBG(debug_evaluation_binop(ans, E_NO_MEM, &v1, &v2, "*"));
            DestroyValue(v1); DestroyValue(v2);
            return E_NO_MEM;
        }
        *ans->v.str = 0;
        ptr = ans->v.str;
        for (int i=0; i<rep; i++) {
            strcat(ptr, str);
            ptr += l;
        }
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "*"));
        DestroyValue(v1); DestroyValue(v2);
        return OK;
    }
    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "*"));
    DestroyValue(v1); DestroyValue(v2);
    return E_BAD_TYPE;
}

static int
divide(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1, v2;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    r = evaluate_expr_node(node->child->sibling, locals, &v2, nonconst);
    if (r != OK) {
        DestroyValue(v1);
        return r;
    }
    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
	if (v2.v.val == 0) {
            DBG(debug_evaluation_binop(ans, E_DIV_ZERO, &v1, &v2, "/"));
            return E_DIV_ZERO;
        }
        /* This is the only way it can overflow */
        if (v2.v.val == -1 && v1.v.val == INT_MIN) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "/"));
            return E_2HIGH;
        }
        *ans = v1;
	ans->v.val /= v2.v.val;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "/"));
	return OK;
    }
    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "/"));
    DestroyValue(v1);
    DestroyValue(v2);
    return E_BAD_TYPE;
}

static int
do_mod(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1, v2;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    r = evaluate_expr_node(node->child->sibling, locals, &v2, nonconst);
    if (r != OK) {
        DestroyValue(v1);
        return r;
    }
    if (v1.type == INT_TYPE && v2.type == INT_TYPE) {
	if (v2.v.val == 0) {
            DBG(debug_evaluation_binop(ans, E_DIV_ZERO, &v1, &v2, "%%"));
            return E_DIV_ZERO;
        }
        /* This is the only way it can overflow */
        if (v2.v.val == -1 && v1.v.val == INT_MIN) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "%%"));
            return E_2HIGH;
        }
        *ans = v1;
	ans->v.val %= v2.v.val;
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "%%"));
	return OK;
    }

    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "%%"));
    DestroyValue(v1);
    DestroyValue(v2);
    return E_BAD_TYPE;
}

static int
logical_not(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    if (v1.type != INT_TYPE) {
        DBG(debug_evaluation_unop(ans, E_BAD_TYPE, &v1, "!"));
        DestroyValue(v1);
        return E_BAD_TYPE;
    }
    ans->type = INT_TYPE;
    ans->v.val = !(v1.v.val);
    DBG(debug_evaluation_unop(ans, OK, &v1, "!"));
    return OK;
}

static int
unary_minus(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    if (v1.type != INT_TYPE) {
        DBG(debug_evaluation_unop(ans, E_BAD_TYPE, &v1, "-"));
        DestroyValue(v1);
        return E_BAD_TYPE;
    }
    ans->type = INT_TYPE;
    ans->v.val = -(v1.v.val);
    DBG(debug_evaluation_unop(ans, OK, &v1, "-"));
    return OK;
}

static int
logical_or(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    Value v;
    int r = evaluate_expr_node(node->child, locals, &v, nonconst);
    if (r != OK) return r;

    if (v.type == STR_TYPE) {
        DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v, NULL, "||"));
        DestroyValue(v);
        return E_BAD_TYPE;
    }
    if (v.v.val) {
        *ans = v;
        DBG(debug_evaluation_binop(ans, OK, &v, NULL, "||"));
        return OK;
    }
    r = evaluate_expr_node(node->child->sibling, locals, ans, nonconst);
    if (r == OK && ans->type == STR_TYPE) {
        DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v, ans, "||"));
        DestroyValue(*ans);
        return E_BAD_TYPE;
    }
    DBG(debug_evaluation_binop(ans, r, &v, ans, "||"));
    return r;
}

static int
logical_and(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    Value v;
    int r = evaluate_expr_node(node->child, locals, &v, nonconst);
    if (r != OK) return r;

    if (v.type == STR_TYPE) {
        DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v, NULL, "&&"));
        DestroyValue(v);
        return E_BAD_TYPE;
    }
    if (!v.v.val) {
        ans->type = v.type;
        ans->v.val = 0;
        DBG(debug_evaluation_binop(ans, OK, &v, NULL, "&&"));
        return OK;
    }
    r = evaluate_expr_node(node->child->sibling, locals, ans, nonconst);
    if (r == OK && ans->type == STR_TYPE) {
        DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v, NULL, "&&"));
        DestroyValue(*ans);
        return E_BAD_TYPE;
    }
    DBG(debug_evaluation_binop(ans, r, &v, ans, "&&"));
    return r;
}

/***************************************************************/
/*                                                             */
/*  parse_expr_token                                           */
/*                                                             */
/*  Read a token.                                              */
/*                                                             */
/***************************************************************/
static int parse_expr_token_aux(DynamicBuffer *buf, char const **in);
static int parse_expr_token(DynamicBuffer *buf, char const **in)
{
    int r = parse_expr_token_aux(buf, in);
/*    if (r == OK) {
        fprintf(stderr, "tok: %s\n", DBufValue(buf));
    } else {
        fprintf(stderr, "err: %s\n", ErrMsg[r]);
        }*/
    return r;
}

static int parse_expr_token_aux(DynamicBuffer *buf, char const **in)
{

    char c;

    DBufFree(buf);

    /* Skip white space */
    while (**in && isempty(**in)) (*in)++;

    if (!**in) return OK;

    c = *(*in)++;
    if (DBufPutc(buf, c) != OK) {
	DBufFree(buf);
	return E_NO_MEM;
    }

    switch(c) {
    case COMMA:
    case END_OF_EXPR:
    case '+':
    case '-':
    case '*':
    case '/':
    case '(':
    case ')':
    case '%': return OK;

    case '&':
    case '|':
    case '=':
	if (**in == c) {
	    if (DBufPutc(buf, c) != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    (*in)++;
	}
	return OK;

    case '!':
    case '>':
    case '<':
	if (**in == '=') {
	    if (DBufPutc(buf, '=') != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    (*in)++;
	}
	return OK;
    }


    /* Handle the parsing of quoted strings */
    if (c == '\"') {
	if (!**in) return E_MISS_QUOTE;
	while (**in) {
	    /* Allow backslash-escapes */
	    if (**in == '\\') {
		int r;
		(*in)++;
		if (!**in) {
		    DBufFree(buf);
		    return E_MISS_QUOTE;
		}
		switch(**in) {
		case 'a':
		    r = DBufPutc(buf, '\a');
		    break;
		case 'b':
		    r = DBufPutc(buf, '\b');
		    break;
		case 'f':
		    r = DBufPutc(buf, '\f');
		    break;
		case 'n':
		    r = DBufPutc(buf, '\n');
		    break;
		case 'r':
		    r = DBufPutc(buf, '\r');
		    break;
		case 't':
		    r = DBufPutc(buf, '\t');
		    break;
		case 'v':
		    r = DBufPutc(buf, '\v');
		    break;
		default:
		    r = DBufPutc(buf, **in);
		}
		(*in)++;
		if (r != OK) {
		    DBufFree(buf);
		    return E_NO_MEM;
		}
		continue;
	    }
	    c = *(*in)++;
	    if (DBufPutc(buf, c) != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    if (c == '\"') break;
	}
	if (c == '\"') return OK;
	DBufFree(buf);
	return E_MISS_QUOTE;
    }

    /* Dates can be specified with single-quotes */
    if (c == '\'') {
	if (!**in) return E_MISS_QUOTE;
	while (**in) {
	    c = *(*in)++;
	    if (DBufPutc(buf, c) != OK) {
		DBufFree(buf);
		return E_NO_MEM;
	    }
	    if (c == '\'') break;
	}
	if (c == '\'') return OK;
	DBufFree(buf);
	return E_MISS_QUOTE;
    }

    if (!ISID(c) && c != '$') {
	Eprint("%s `%c'", ErrMsg[E_ILLEGAL_CHAR], c);
	return E_ILLEGAL_CHAR;
    }

    if (c == '$' && **in && isalpha(**in)) {
        while(ISID(**in)) {
            if (DBufPutc(buf, **in) != OK) {
                DBufFree(buf);
                return E_NO_MEM;
            }
            (*in)++;
        }
        return OK;
    }

    /* Parse a constant, variable name or function */
    while (ISID(**in) || **in == ':' || **in == '.' || **in == TimeSep) {
	if (DBufPutc(buf, **in) != OK) {
	    DBufFree(buf);
	    return E_NO_MEM;
	}
	(*in)++;
    }
    /* Chew up any remaining white space */
    while (**in && isempty(**in)) (*in)++;

    /* Peek ahead - is it an id followed by '('?  Then we have a function call */
    if (isalpha(*(DBufValue(buf))) ||
        *DBufValue(buf) == '_') {
        if (**in == '(') {
            if (DBufPutc(buf, '(') != OK) {
                DBufFree(buf);
                return E_NO_MEM;
            }
            (*in)++;
        }
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  peek_expr_token                                            */
/*                                                             */
/*  Read a token without advancing the input pointer           */
/*                                                             */
/***************************************************************/
static int peek_expr_token(DynamicBuffer *buf, char const *in)
{
    int r = parse_expr_token_aux(buf, &in);

/*    if (r == OK) {
        fprintf(stderr, "peek-tok: %s\n", DBufValue(buf));
    } else {
        fprintf(stderr, "peek-err: %s\n", ErrMsg[r]);
        } */
    return r;
}

/* Recursively free an expression tree */
expr_node *
free_expr_tree(expr_node *node)
{
    if (node) {
        ExprNodesUsed--;
        if (node->type == N_CONSTANT ||
            node->type == N_VARIABLE ||
            node->type == N_SYSVAR   ||
            node->type == N_USER_FUNC) {
            DestroyValue(node->u.value);
        }
        free_expr_tree(node->child);
        free_expr_tree(node->sibling);
        node->child = (expr_node *) expr_node_free_list;
        expr_node_free_list = (void *) node;
        node->type = N_FREE;
    }
    return NULL;
}

static int set_long_name(expr_node *node, char const *s)
{
    char *buf;
    size_t len = strlen(s);
    if (len > VAR_NAME_LEN) len = VAR_NAME_LEN;
    buf = malloc(len+1);
    if (!buf) {
        return E_NO_MEM;
    }
    StrnCpy(buf, s, VAR_NAME_LEN);
    node->u.value.type = STR_TYPE;
    node->u.value.v.str = buf;
    return OK;
}

static expr_node *
parse_function_call(char const **e, int *r, Var *locals)
{
    expr_node *node = alloc_expr_node(r);
    expr_node *arg;
    char *s;

    if (!node) {
        return NULL;
    }
    s = DBufValue(&ExprBuf);
    *(s + DBufLen(&ExprBuf) - 1) = 0;
    BuiltinFunc *f = FindBuiltinFunc(s);
    if (f) {
        node->u.builtin_func = f;
        node->type = N_BUILTIN_FUNC;
    } else {
        if (strlen(s) < SHORT_NAME_BUF) {
            node->type = N_SHORT_USER_FUNC;
            strcpy(node->u.name, s);
            strtolower(node->u.name);
        } else {
            if (set_long_name(node, s) != OK) {
                *r = E_NO_MEM;
                return free_expr_tree(node);
            }
            strtolower(node->u.value.v.str);
            node->type = N_USER_FUNC;
        }
    }
    /* Now parse the arguments */
    *r = GET_TOKEN();
    if (*r != OK) {
        free_expr_tree(node);
        return NULL;
    }
    while(TOKEN_ISNOT(")")) {
        *r = PEEK_TOKEN();
        if (*r != OK) {
                return free_expr_tree(node);
        }
        if (TOKEN_IS(")")) {
            continue;
        }
        arg = parse_expression_aux(e, r, locals);
        if (*r != OK) {
            free_expr_tree(node);
            return NULL;
        }
        add_child(node, arg);
        *r = PEEK_TOKEN();
        if (*r != OK) {
                return free_expr_tree(node);
        }
        if (TOKEN_ISNOT(")") &&
            TOKEN_ISNOT(",")) {
            *r = E_EXPECT_COMMA;
            return free_expr_tree(node);
        }
        if (TOKEN_IS(",")) {
            *r = GET_TOKEN();
            if (*r != OK) {
                return free_expr_tree(node);
            }
            *r = PEEK_TOKEN();
            if (*r != OK) {
                return free_expr_tree(node);
            }
            if (TOKEN_IS(")")) {
                Eprint("%s `)'", ErrMsg[E_ILLEGAL_CHAR]);
                *r = E_ILLEGAL_CHAR;
                return free_expr_tree(node);
            }
        }
    }
    if (TOKEN_IS(")")) {
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(node);
        }
    }
    /* Check args for builtin funcs */
    if (node->type == N_BUILTIN_FUNC) {
        f = node->u.builtin_func;
        if (node->num_kids < f->minargs) *r = E_2FEW_ARGS;
        if (node->num_kids > f->maxargs && f->maxargs != NO_MAX) *r = E_2MANY_ARGS;
    }
    if (*r != OK) {
        if (node->type == N_BUILTIN_FUNC) {
            f = node->u.builtin_func;
            Eprint("%s: %s", f->name, ErrMsg[*r]);
        }
        return free_expr_tree(node);
    }
    return node;
}

static int set_constant_value(expr_node *atom)
{
    int dse, tim, val, prev_val, h, m, ampm, r;
    size_t len;
    char const *s = DBufValue(&ExprBuf);
    atom->u.value.type = ERR_TYPE;

    ampm = 0;
    if (*s == '\"') { /* It's a literal string "*/
	len = strlen(s)-1;
	atom->u.value.type = STR_TYPE;
	atom->u.value.v.str = malloc(len);
	if (! atom->u.value.v.str) {
	    atom->u.value.type = ERR_TYPE;
	    return E_NO_MEM;
	}
	strncpy(atom->u.value.v.str, s+1, len-1);
	*(atom->u.value.v.str+len-1) = 0;
	return OK;
    } else if (*s == '\'') { /* It's a literal date */
	s++;
	if ((r=ParseLiteralDate(&s, &dse, &tim)) != 0) return r;
	if (*s != '\'') return E_BAD_DATE;
	if (tim == NO_TIME) {
	    atom->u.value.type = DATE_TYPE;
	    atom->u.value.v.val = dse;
	} else {
	    atom->u.value.type = DATETIME_TYPE;
	    atom->u.value.v.val = (dse * MINUTES_PER_DAY) + tim;
	}
	return OK;
    } else if (isdigit(*s)) { /* It's a number or time */
        atom->u.value.type = INT_TYPE;
        val = 0;
        prev_val = 0;
	while (*s && isdigit(*s)) {
	    val *= 10;
	    val += (*s++ - '0');
            if (val < prev_val) {
                /* We overflowed */
                return E_2HIGH;
            }
            prev_val = val;
	}
	if (*s == ':' || *s == '.' || *s == TimeSep) { /* Must be a literal time */
	    s++;
	    if (!isdigit(*s)) return E_BAD_TIME;
	    h = val;
	    m = 0;
	    while (isdigit(*s)) {
		m *= 10;
		m += *s - '0';
		s++;
	    }
	    /* Check for p[m] or a[m] */
	    if (*s == 'A' || *s == 'a' || *s == 'P' || *s == 'p') {
		ampm = tolower(*s);
		s++;
		if (*s == 'm' || *s == 'M') {
		    s++;
		}
	    }
	    if (*s || h>23 || m>59) return E_BAD_TIME;
	    if (ampm) {
		if (h < 1 || h > 12) return E_BAD_TIME;
		if (ampm == 'a') {
		    if (h == 12) {
			h = 0;
		    }
		} else if (ampm == 'p') {
		    if (h < 12) {
			h += 12;
		    }
		}
	    }
	    atom->u.value.type = TIME_TYPE;
	    atom->u.value.v.val = h*60 + m;
	    return OK;
	}
	/* Not a time - must be a number */
	if (*s) return E_BAD_NUMBER;
	atom->u.value.type = INT_TYPE;
	atom->u.value.v.val = val;
	return OK;
    }
    atom->u.value.type = ERR_TYPE;
    Eprint("`%s': %s", DBufValue(&ExprBuf), ErrMsg[E_ILLEGAL_CHAR]);
    return E_ILLEGAL_CHAR;
}

static int make_atom(expr_node *atom, Var *locals)
{
    int r;
    int i = 0;
    Var *v = locals;
    char const *s = DBufValue(&ExprBuf);
    /* Variable */
    if (isalpha(*s) || *s == '_') {
        while(v) {
            if (! StrinCmp(s, v->name, VAR_NAME_LEN)) {
                atom->type = N_LOCAL_VAR;
                atom->u.arg = i;
                return OK;
            }
            v = v->next;
            i++;
        }
        if (strlen(s) < SHORT_NAME_BUF) {
            atom->type = N_SHORT_VAR;
            strcpy(atom->u.name, s);
        } else {
            if (set_long_name(atom, s) != OK) {
                return E_NO_MEM;
            }
            atom->type = N_VARIABLE;
        }
        return OK;
    }

    /* System Variable */
    if (*(s) == '$' && isalpha(*(s+1))) {
        if (!FindSysVar(s+1)) {
            Eprint("`%s': %s", s, ErrMsg[E_NOSUCH_VAR]);
            return E_NOSUCH_VAR;
        }
        if (strlen(s+1) < SHORT_NAME_BUF) {
            atom->type = N_SHORT_SYSVAR;
            strcpy(atom->u.name, s+1);
        } else {
            if (set_long_name(atom, s+1) != OK) {
                return E_NO_MEM;
            }
            atom->type = N_SYSVAR;
        }
        return OK;
    }

    /* Constant */
    r = set_constant_value(atom);
    if (r == OK) {
        atom->type = N_CONSTANT;
    } else {
        atom->type = N_ERROR;
    }
    return r;
}

static expr_node *
parse_atom(char const **e, int *r, Var *locals)
{
    expr_node *node;
    char const *s;
    *r = PEEK_TOKEN();
    if (*r != OK) return  NULL;

    /* Ignore unary-plus operators */
    while (TOKEN_IS("+")) {
        *r = GET_TOKEN();
        if (*r != OK) {
            return NULL;
        }
    }

    if (TOKEN_IS("(")) {
        /* Parenthesiszed expession:  '('   EXPR   ')' */

        /* Pull off the peeked token */
        *r = GET_TOKEN();
        if (*r != OK) {
            return NULL;
        }
        node = parse_expression_aux(e, r, locals);
        if (*r != OK) {
            return NULL;
        }
        if (TOKEN_ISNOT(")")) {
            *r = E_MISS_RIGHT_PAREN;
            return free_expr_tree(node);
        }
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(node);
        }
        return node;
    }

    /* Check that it's a valid ID or constant */
    s = DBufValue(&ExprBuf);
    if (!ISID(*s) &&
        *s != '%' &&
        *s != '$' &&
        *s != '"' &&
        *s != '\'') {
        Eprint("%s `%c'", ErrMsg[E_ILLEGAL_CHAR], *s);
        *r = E_ILLEGAL_CHAR;
        return NULL;
    }

    /* Is it a function call? */
    if (*(s + DBufLen(&ExprBuf) - 1) == '(') {
        return parse_function_call(e, r, locals);
    }

    /* It's a constant or a variable reference */
    *r = GET_TOKEN();
    if (*r != OK) return NULL;
    node = alloc_expr_node(r);
    if (!node) {
        return NULL;
    }
    *r = make_atom(node, locals);
    if (*r != OK) {
        return free_expr_tree(node);
    }
    return node;
}

/*
 * EXPR:      OR_EXP                  |
 *            OR_EXP '||' EXPR
 * OR_EXP:    AND_EXP                 |
 *            AND_EXP '&&' OR_EXP
 * AND_EXP:   EQ_EXP                  |
 *            EQ_EXP '==' AND_EXP     |
 *            EQ_EXP '!=' AND_EXP
 * EQ_EXP:    CMP_EXP                 |
 *            CMP_EXP '<' EQ_EXP      |
 *            CMP_EXP '>' EQ_EXP      |
 *            CMP_EXP '<=' EQ_EXP     |
 *            CMP_EXP '<=' EQ_EXP     |
 * CMP_EXP:   TERM_EXP                |
 *            TERM_EXP '+' CMP_EXP    |
 *            TERM_EXP '-' CMP_EXP
 * TERM_EXP:  FACTOR_EXP              |
 *            FACTOR_EXP '*' TERM_EXP |
 *            FACTOR_EXP '/' TERM_EXP |
 *            FACTOR_EXP '%' TERM_EXP
 * FACTOR_EXP: '-' FACTOR_EXP         |
 *             '!' FACTOR_EXP         |
 *            ATOM
 * ATOM:      '+' ATOM                |
 *            '(' EXPR ')'            |
 *            CONSTANT                |
 *            VAR                     |
 *            FUNCTION_CALL
 */

/*
 * FACTOR_EXP: '-' FACTOR_EXP         |
 *             '!' FACTOR_EXP         |
 *            ATOM
 */
static expr_node *
parse_factor(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *factor_node;
    char op;
    *r = PEEK_TOKEN();
    if (*r != OK) {
        return NULL;
    }
    if (TOKEN_IS("!") || TOKEN_IS("-")) {
        if (TOKEN_IS("!")) {
            op = '!';
        } else {
            op = '-';
        }
        /* Pull off the peeked token */
        GET_TOKEN();
        node = parse_factor(e, r, locals);
        if (*r != OK) {
            return NULL;
        }

        /* If the child is a constant int, optimize! */
        if (node->type == N_CONSTANT &&
            node->u.value.type == INT_TYPE) {
            if (op == '-') {
                node->u.value.v.val = -node->u.value.v.val;
            } else {
                node->u.value.v.val = !node->u.value.v.val;
            }
            return node;
        }

        /* Not a constant int; we need to add a node */
        factor_node = alloc_expr_node(r);
        if (!factor_node) {
            free_expr_tree(node);
            return NULL;
        }
        factor_node->type = N_OPERATOR;
        if (op == '!') {
            factor_node->u.operator_func = logical_not;
        } else {
            factor_node->u.operator_func = unary_minus;
        }
        add_child(factor_node, node);
        return factor_node;
    }
    return parse_atom(e, r, locals);
}

/*
 * TERM_EXP:  FACTOR_EXP              |
 *            FACTOR_EXP '*' TERM_EXP |
 *            FACTOR_EXP '/' TERM_EXP |
 *            FACTOR_EXP '%' TERM_EXP
 */
static expr_node *
parse_term_expr(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *term_node;

    node = parse_factor(e, r, locals);
    if (*r != OK) {
        return free_expr_tree(node);
    }
    *r = PEEK_TOKEN();
    if (*r != OK) {
        return free_expr_tree(node);
    }

    while(TOKEN_IS("*") || TOKEN_IS("/") || TOKEN_IS("%")) {
        term_node = alloc_expr_node(r);
        if (!term_node) {
            return free_expr_tree(node);
        }
        term_node->type = N_OPERATOR;
        if (TOKEN_IS("*")) {
            term_node->u.operator_func = multiply;
        } else if (TOKEN_IS("/")) {
            term_node->u.operator_func = divide;
        } else {
            term_node->u.operator_func = do_mod;
        }
        add_child(term_node, node);
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(term_node);
        }
        node = parse_factor(e, r, locals);
        if (*r != OK) {
            return free_expr_tree(term_node);
        }
        add_child(term_node, node);
        node = term_node;
        *r = PEEK_TOKEN();
        if (*r != OK) {
            return free_expr_tree(term_node);
        }
    }
    return node;
}

/*
 * CMP_EXP:   TERM_EXP                |
 *            TERM_EXP '+' CMP_EXP    |
 *            TERM_EXP '-' CMP_EXP
 */
static expr_node *
parse_cmp_expr(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *cmp_node;

    node = parse_term_expr(e, r, locals);
    if (*r != OK) {
        return free_expr_tree(node);
    }
    while(TOKEN_IS("+") || TOKEN_IS("-")) {
        cmp_node = alloc_expr_node(r);
        if (!cmp_node) {
            return free_expr_tree(node);
        }
        cmp_node->type = N_OPERATOR;
        if (TOKEN_IS("+")) {
            cmp_node->u.operator_func = add;
        } else {
            cmp_node->u.operator_func = subtract;
        }
        add_child(cmp_node, node);
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(cmp_node);
        }
        node = parse_term_expr(e, r, locals);
        if (*r != OK) {
            return free_expr_tree(cmp_node);
        }
        add_child(cmp_node, node);
        node = cmp_node;
    }
    return node;
}

/*
 * EQ_EXP:    CMP_EXP                 |
 *            CMP_EXP '<' EQ_EXP      |
 *            CMP_EXP '>' EQ_EXP      |
 *            CMP_EXP '<=' EQ_EXP     |
 *            CMP_EXP '<=' EQ_EXP     |
 */
static expr_node *
parse_eq_expr(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *eq_node;

    node = parse_cmp_expr(e, r, locals);
    if (*r != OK) {
        return free_expr_tree(node);
    }
    while(TOKEN_IS("<=") || TOKEN_IS(">=") || TOKEN_IS("<") || TOKEN_IS(">")) {
        eq_node = alloc_expr_node(r);
        if (!eq_node) {
            return free_expr_tree(node);
        }
        eq_node->type = N_OPERATOR;
        if (TOKEN_IS("<=")) {
            eq_node->u.operator_func = compare_le;
        } else if (TOKEN_IS(">=")) {
            eq_node->u.operator_func = compare_ge;
        } else if (TOKEN_IS("<")) {
            eq_node->u.operator_func = compare_lt;
        } else {
            eq_node->u.operator_func = compare_gt;
        }
        add_child(eq_node, node);
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(eq_node);
        }
        node = parse_cmp_expr(e, r, locals);
        if (*r != OK) {
            free_expr_tree(eq_node);
            return free_expr_tree(node);
        }
        add_child(eq_node, node);
        node = eq_node;
    }
    return node;
}

/*
 * AND_EXP:   EQ_EXP                  |
 *            EQ_EXP '==' AND_EXP     |
 *            EQ_EXP '!=' AND_EXP
 */
static expr_node *
parse_and_expr(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *and_node;

    node = parse_eq_expr(e, r, locals);
    if (*r != OK) {
        return free_expr_tree(node);
    }
    while(TOKEN_IS("==") || TOKEN_IS("!=")) {
        and_node = alloc_expr_node(r);
        if (!and_node) {
            return free_expr_tree(node);
        }
        and_node->type = N_OPERATOR;
        if (TOKEN_IS("==")) {
            and_node->u.operator_func = compare_eq;
        } else {
            and_node->u.operator_func = compare_ne;
        }
        add_child(and_node, node);
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(and_node);
        }
        node = parse_eq_expr(e, r, locals);
        if (*r != OK) {
            return free_expr_tree(and_node);
        }
        add_child(and_node, node);
        node = and_node;
    }
    return node;
}

static expr_node *
parse_or_expr(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *logand_node;

    node = parse_and_expr(e, r, locals);

    if (*r != OK) {
        return free_expr_tree(node);
    }

    while (TOKEN_IS("&&")) {
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(node);
        }
        logand_node = alloc_expr_node(r);
        if (!logand_node) {
            return free_expr_tree(node);
        }
        logand_node->type = N_OPERATOR;
        logand_node->u.operator_func = logical_and;
        add_child(logand_node, node);
        node = parse_and_expr(e, r, locals);
        if (*r != OK) {
            free_expr_tree(logand_node);
            return free_expr_tree(node);
        }
        add_child(logand_node, node);
        node = logand_node;
    }
    return node;
}

static expr_node *
parse_expression_aux(char const **e, int *r, Var *locals)
{
    expr_node *node;
    expr_node *logor_node;
    node = parse_or_expr(e, r, locals);

    if (*r != OK) {
        return free_expr_tree(node);
    }
    while (TOKEN_IS("||")) {
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(node);
        }
        logor_node = alloc_expr_node(r);
        if (!logor_node) {
            return free_expr_tree(node);
        }
        logor_node->type = N_OPERATOR;
        logor_node->u.operator_func = logical_or;
        add_child(logor_node, node);
        node = parse_or_expr(e, r, locals);
        if (*r != OK) {
            free_expr_tree(logor_node);
            return free_expr_tree(node);
        }
        add_child(logor_node, node);
        node = logor_node;
    }
    return node;
}

expr_node *
parse_expression(char const **e, int *r, Var *locals)
{
    char const *orig = *e;
    expr_node *node = parse_expression_aux(e, r, locals);
    if (DebugFlag & DB_PARSE_EXPR) {
        fprintf(ErrFp, "Parsed expression: ");
        while (*orig && orig != *e) {
            putc(*orig, ErrFp);
            orig++;
        }
        putc('\n', ErrFp);
        if (*r != OK) {
            fprintf(ErrFp, "  => Error: %s\n", ErrMsg[*r]);
        } else {
            fprintf(ErrFp, "  => ");
            print_expr_tree(node, ErrFp);
            fprintf(ErrFp, "\n");
        }
        if (**e && (**e != ']')) {
            fprintf(ErrFp, "  Unparsed: %s\n", *e);
        }
    }
    return node;
}


/* Debugging routines */
static void
print_kids(expr_node *node, FILE *fp)
{
    int done = 0;
    expr_node *kid = node->child;
    while(kid) {
        if (done) {
            fprintf(fp, " ");
        }
        done=1;
        print_expr_tree(kid, fp);
        kid = kid->sibling;
    }
}

void
print_expr_tree(expr_node *node, FILE *fp)
{
    if (!node) {
        return;
    }
    switch(node->type) {
    case N_CONSTANT:
        PrintValue(&(node->u.value), fp);
        return;
    case N_SHORT_VAR:
        fprintf(fp, "%s", node->u.name);
        return;
    case N_VARIABLE:
        fprintf(fp, "%s", node->u.value.v.str);
        return;
    case N_SHORT_SYSVAR:
        fprintf(fp, "$%s", node->u.name);
        return;
    case N_SYSVAR:
        fprintf(fp, "$%s", node->u.value.v.str);
        return;
    case N_LOCAL_VAR:
        fprintf(fp, "arg[%d]", node->u.arg);
        return;
    case N_BUILTIN_FUNC:
        fprintf(fp, "(B:%s", node->u.builtin_func->name);
        if (node->child) fprintf(fp, " ");
        print_kids(node, fp);
        fprintf(fp, ")");
        return;
    case N_SHORT_USER_FUNC:
        fprintf(fp, "(U:%s", node->u.name);
        if (node->child) fprintf(fp, " ");
        print_kids(node, fp);
        fprintf(fp, ")");
        return;
    case N_USER_FUNC:
        fprintf(fp, "(U:%s", node->u.value.v.str);
        if (node->child) fprintf(fp, " ");
        print_kids(node, fp);
        fprintf(fp, ")");
        return;
    case N_OPERATOR:
        fprintf(fp, "(%s ", get_operator_name(node));
        print_kids(node, fp);
        fprintf(fp, ")");
        return;
    default:
        return;
    }
}

static char const *
get_operator_name(expr_node *node)
{
    int (*f)(expr_node *node, Value *locals, Value *ans, int *nonconst) = node->u.operator_func;
    if (f == logical_not) return "!";
    else if (f == unary_minus) return "-";
    else if (f == multiply) return "*";
    else if (f == divide) return "/";
    else if (f == do_mod) return "%%";
    else if (f == add) return "+";
    else if (f == subtract) return "-";
    else if (f == compare_le) return "<=";
    else if (f == compare_ge) return ">=";
    else if (f == compare_lt) return "<";
    else if (f == compare_gt) return ">";
    else if (f == compare_eq) return "==";
    else if (f == compare_ne) return "!=";
    else if (f == logical_and) return "&&";
    else if (f == logical_or) return "||";
    else return "UNKNOWN_OPERATOR";
}

/***************************************************************/
/*                                                             */
/*  EvalExpr                                                   */
/*  Evaluate an expression.  Return 0 if OK, non-zero if error */
/*  Put the result into value pointed to by v.                 */
/*                                                             */
/***************************************************************/
int EvalExpr(char const **e, Value *v, ParsePtr p)
{
    int r;
    int nonconst = 0;

    /* Parse */
    expr_node *n = parse_expression(e, &r, NULL);
    if (r != OK) {
        return r;
    }

    /* Evaluate */
    r = evaluate_expr_node(n, NULL, v, &nonconst);

    /* Throw away the parsed tree */
    free_expr_tree(n);
    if (r != OK) {
        return r;
    }
    if (nonconst) {
        if (p) {
            p->nonconst_expr = 1;
        }
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  PrintValue                                                 */
/*                                                             */
/*  Print or stringify a value for debugging purposes.         */
/*                                                             */
/***************************************************************/
static DynamicBuffer printbuf = {NULL, 0, 0, ""};

#define PV_PUTC(fp, c) do { if (fp) { putc((c), fp); } else { DBufPutc(&printbuf, (c)); } } while(0);
char const *PrintValue (Value *v, FILE *fp)
{
    int y, m, d;
    unsigned char const *s;
    char pvbuf[512];
    if (!fp) {
        /* It's OK to DBufFree an uninitialized *BUT STATIC* dynamic buffer */
        DBufFree(&printbuf);
    }

    if (v->type == STR_TYPE) {
	s = (unsigned char const *) v->v.str;
	PV_PUTC(fp, '"');
	for (y=0; y<MAX_PRT_LEN && *s; y++) {
            switch(*s) {
            case '\a': PV_PUTC(fp, '\\'); PV_PUTC(fp, 'a'); break;
            case '\b': PV_PUTC(fp, '\\'); PV_PUTC(fp, 'b'); break;
            case '\f': PV_PUTC(fp, '\\'); PV_PUTC(fp, 'f'); break;
            case '\n': PV_PUTC(fp, '\\'); PV_PUTC(fp, 'n'); break;
            case '\r': PV_PUTC(fp, '\\'); PV_PUTC(fp, 'r'); break;
            case '\t': PV_PUTC(fp, '\\'); PV_PUTC(fp, 't'); break;
            case '\v': PV_PUTC(fp, '\\'); PV_PUTC(fp, 'v'); break;
            case '"':  PV_PUTC(fp, '\\'); PV_PUTC(fp, '"'); break;
            case '\\': PV_PUTC(fp, '\\'); PV_PUTC(fp, '\\'); break;
            default:
                if (*s < 32) {
                    if (fp) {
                        fprintf(fp, "\\x%02x", (unsigned int) *s);
                    } else {
                        snprintf(pvbuf, sizeof(pvbuf), "\\x%02x", (unsigned int) *s);
                        DBufPuts(&printbuf, pvbuf);
                    }
                } else {
                    PV_PUTC(fp, *s); break;
                }
            }
            s++;
        }
        PV_PUTC(fp, '"');
	if (*s) {
            if (fp) {
                fprintf(fp, "...");
            } else {
                DBufPuts(&printbuf, "...");
            }
        }
    }
    else if (v->type == INT_TYPE) {
        if (fp) {
            fprintf(fp, "%d", v->v.val);
        } else {
            snprintf(pvbuf, sizeof(pvbuf), "%d", v->v.val);
            DBufPuts(&printbuf, pvbuf);
        }
    } else if (v->type == TIME_TYPE) {
        if (fp) {
            fprintf(fp, "%02d%c%02d", v->v.val / 60,
                    TimeSep, v->v.val % 60);
        } else {
            snprintf(pvbuf, sizeof(pvbuf), "%02d%c%02d", v->v.val / 60,
                    TimeSep, v->v.val % 60);
            DBufPuts(&printbuf, pvbuf);
        }
    } else if (v->type == DATE_TYPE) {
	FromDSE(v->v.val, &y, &m, &d);
        if (fp) {
            fprintf(fp, "%04d%c%02d%c%02d", y, DateSep, m+1, DateSep, d);
        } else {
            snprintf(pvbuf, sizeof(pvbuf), "%04d%c%02d%c%02d", y, DateSep, m+1, DateSep, d);
            DBufPuts(&printbuf, pvbuf);
        }
    } else if (v->type == DATETIME_TYPE) {
	FromDSE(v->v.val / MINUTES_PER_DAY, &y, &m, &d);
        if (fp) {
            fprintf(fp, "%04d%c%02d%c%02d%c%02d%c%02d", y, DateSep, m+1, DateSep, d, DateTimeSep,
                    (v->v.val % MINUTES_PER_DAY) / 60, TimeSep, (v->v.val % MINUTES_PER_DAY) % 60);
        } else {
            snprintf(pvbuf, sizeof(pvbuf), "%04d%c%02d%c%02d%c%02d%c%02d", y, DateSep, m+1, DateSep, d, DateTimeSep,
                    (v->v.val % MINUTES_PER_DAY) / 60, TimeSep, (v->v.val % MINUTES_PER_DAY) % 60);
            DBufPuts(&printbuf, pvbuf);
        }
    } else {
        if (fp) {
            fprintf(fp, "ERR");
        } else {
            DBufPuts(&printbuf, "ERR");
        }
    }
    if (fp) {
        return NULL;
    } else {
        return DBufValue(&printbuf);
    }
}

/***************************************************************/
/*                                                             */
/*  CopyValue                                                  */
/*                                                             */
/*  Copy a value.                                              */
/*                                                             */
/***************************************************************/
int CopyValue(Value *dest, const Value *src)
{
    dest->type = ERR_TYPE;
    if (src->type == STR_TYPE) {
	dest->v.str = StrDup(src->v.str);
	if (!dest->v.str) return E_NO_MEM;
    } else {
	dest->v.val = src->v.val;
    }
    dest->type = src->type;
    return OK;
}

int ParseLiteralTime(char const **s, int *tim)
{
    int h=0;
    int m=0;
    int ampm=0;
    if (!isdigit(**s)) return E_BAD_TIME;
    while(isdigit(**s)) {
	h *= 10;
	h += *(*s)++ - '0';
    }
    if (**s != ':' && **s != '.' && **s != TimeSep) return E_BAD_TIME;
    (*s)++;
    if (!isdigit(**s)) return E_BAD_TIME;
    while(isdigit(**s)) {
	m *= 10;
	m += *(*s)++ - '0';
    }
    /* Check for p[m] or a[m] */
    if (**s == 'A' || **s == 'a' || **s == 'P' || **s == 'p') {
	ampm = tolower(**s);
	(*s)++;
	if (**s == 'm' || **s == 'M') {
	    (*s)++;
	}
    }
    if (h>23 || m>59) return E_BAD_TIME;
    if (ampm) {
	if (h < 1 || h > 12) return E_BAD_TIME;
	if (ampm == 'a') {
	    if (h == 12) {
		h = 0;
	    }
	} else if (ampm == 'p') {
	    if (h < 12) {
		h += 12;
	    }
	}
    }
    *tim = h * 60 + m;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  ParseLiteralDate                                           */
/*                                                             */
/*  Parse a literal date or datetime.  Return result in dse    */
/*  and tim; update s.                                         */
/*                                                             */
/***************************************************************/
int ParseLiteralDate(char const **s, int *dse, int *tim)
{
    int y, m, d;
    int r;

    y=0; m=0; d=0;

    *tim = NO_TIME;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
	y *= 10;
	y += *(*s)++ - '0';
    }
    if (**s != '/' && **s != '-' && **s != DateSep) return E_BAD_DATE;
    (*s)++;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
	m *= 10;
	m += *(*s)++ - '0';
    }
    m--;
    if (**s != '/' && **s != '-' && **s != DateSep) return E_BAD_DATE;
    (*s)++;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
	d *= 10;
	d += *(*s)++ - '0';
    }
    if (!DateOK(y, m, d)) return E_BAD_DATE;

    *dse = DSE(y, m, d);

    /* Do we have a time part as well? */
    if (**s == ' ' || **s == '@' || **s == 'T' || **s == 't') {
	(*s)++;
	r = ParseLiteralTime(s, tim);
	if (r != OK) return r;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  DoCoerce - actually coerce a value to the specified type.  */
/*                                                             */
/***************************************************************/
int DoCoerce(char type, Value *v)
{
    int h, d, m, y, i, k;
    char const *s;

    char coerce_buf[128];

    /* Do nothing if value is already the right type */
    if (type == v->type) return OK;

    switch(type) {
    case DATETIME_TYPE:
	switch(v->type) {
	case INT_TYPE:
	    v->type = DATETIME_TYPE;
	    return OK;
	case DATE_TYPE:
	    v->type = DATETIME_TYPE;
	    v->v.val *= MINUTES_PER_DAY;
	    return OK;
	case STR_TYPE:
	    s = v->v.str;
	    if (ParseLiteralDate(&s, &i, &m)) return E_CANT_COERCE;
	    if (*s) return E_CANT_COERCE;
	    v->type = DATETIME_TYPE;
	    free(v->v.str);
	    if (m == NO_TIME) m = 0;
	    v->v.val = i * MINUTES_PER_DAY + m;
	    return OK;
	default:
	    return E_CANT_COERCE;
	}
    case STR_TYPE:
	switch(v->type) {
	case INT_TYPE: sprintf(coerce_buf, "%d", v->v.val); break;
	case TIME_TYPE: sprintf(coerce_buf, "%02d%c%02d", v->v.val / 60,
			       TimeSep, v->v.val % 60);
	break;
	case DATE_TYPE: FromDSE(v->v.val, &y, &m, &d);
	    sprintf(coerce_buf, "%04d%c%02d%c%02d",
		    y, DateSep, m+1, DateSep, d);
	    break;
	case DATETIME_TYPE:
	    i = v->v.val / MINUTES_PER_DAY;
	    FromDSE(i, &y, &m, &d);
	    k = v->v.val % MINUTES_PER_DAY;
	    h = k / 60;
	    i = k % 60;
	    sprintf(coerce_buf, "%04d%c%02d%c%02d%c%02d%c%02d",
		    y, DateSep, m+1, DateSep, d, DateTimeSep, h, TimeSep, i);
	    break;
	default: return E_CANT_COERCE;
	}
	v->type = STR_TYPE;
	v->v.str = StrDup(coerce_buf);
	if (!v->v.str) {
	    v->type = ERR_TYPE;
	    return E_NO_MEM;
	}
	return OK;

    case INT_TYPE:
	i = 0;
	m = 1;
	switch(v->type) {
	case STR_TYPE:
	    s = v->v.str;
	    if (*s == '-') {
		m = -1;
		s++;
	    }
	    while(*s && isdigit(*s)) {
		i *= 10;
		i += (*s++) - '0';
	    }
	    if (*s) {
		free (v->v.str);
		v->type = ERR_TYPE;
		return E_CANT_COERCE;
	    }
	    free(v->v.str);
	    v->type = INT_TYPE;
	    v->v.val = i * m;
	    return OK;

	case DATE_TYPE:
	case TIME_TYPE:
	case DATETIME_TYPE:
	    v->type = INT_TYPE;
	    return OK;

	default: return E_CANT_COERCE;
	}

    case DATE_TYPE:
	switch(v->type) {
	case INT_TYPE:
	    if(v->v.val >= 0) {
		v->type = DATE_TYPE;
		return OK;
	    } else return E_2LOW;

	case STR_TYPE:
	    s = v->v.str;
	    if (ParseLiteralDate(&s, &i, &m)) return E_CANT_COERCE;
	    if (*s) return E_CANT_COERCE;
	    v->type = DATE_TYPE;
	    free(v->v.str);
	    v->v.val = i;
	    return OK;

	case DATETIME_TYPE:
	    v->type = DATE_TYPE;
	    v->v.val /= MINUTES_PER_DAY;
	    return OK;

	default: return E_CANT_COERCE;
	}

    case TIME_TYPE:
	switch(v->type) {
	case INT_TYPE:
	case DATETIME_TYPE:
	    v->type = TIME_TYPE;
	    v->v.val %= MINUTES_PER_DAY;
	    if (v->v.val < 0) v->v.val += MINUTES_PER_DAY;
	    return OK;

	case STR_TYPE:
	    s = v->v.str;
	    if (ParseLiteralTime(&s, &i)) return E_CANT_COERCE;
	    if (*s) return E_CANT_COERCE;
	    v->type = TIME_TYPE;
	    free(v->v.str);
	    v->v.val = i;
	    return OK;

	default: return E_CANT_COERCE;
	}
    default: return E_CANT_COERCE;
    }
}

void
print_expr_nodes_stats(void)
{
    fprintf(stderr, " Expression nodes allocated: %d (%u bytes)\n", ExprNodesAllocated, (unsigned) (ExprNodesAllocated * sizeof(expr_node)));
    fprintf(stderr, "Expression nodes high-water: %d\n", ExprNodesHighWater);
    fprintf(stderr, "    Expression nodes leaked: %d\n", ExprNodesUsed);
}
