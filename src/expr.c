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
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/
#include "config.h"
#include "err.h"
#include "types.h"
#include "protos.h"
#include "globals.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

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
  8) N_SHORT_STR:    A string constant short enough to store in u.name
  9) N_ERROR:        An uninitialized node, or a parse error

  Additional types are N_SHORT_VAR, N_SHORT_SYSVAR, and N_SHORT_USER_FUNC
  which behave identically to N_VARIABLE, N_SYSVAR and N_USER_FUNC
  respectively, but have short-enough names to be stored more efficiently
  in the expr_node object.

  expr_nodes contain the following data, depending on their type:

  1) N_CONSTANT: The constant value is stored in the node's u.value field
  2) N_LOCAL_VAR: The offset into the function's argument list is stored in
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

  Nodes may have an arbitrary number of children, represented in the standard
  two-pointer left-child, right-sibling representation.

  As an example, the expression: 3 + 5 * min(x, 4) is represented like this:

          +----------+
          | OPERATOR |
          |    +     |
          +----------+
             /       \
      +----------+  +----------+
      | CONSTANT |  | OPERATOR |
      |     3    |  |    *     |
      +----------+  +----------+
                      /       \
            +----------+   +---------+
            | CONSTANT |   | BUILTIN |
            |     5    |   |   min   |
            +----------+   +---------+
                             /     \
                   +----------+   +----------+
                   | VARIABLE |   | CONSTANT |
                   |     x    |   |    4     |
                   +----------+   +----------+

  Evaluation is done recursively from the root of the tree.  To
  evaluate a node N:

  1) Evaluate node N's children from left to right
  2) Apply the operator or function to all of the resulting values

  For nodes without children, the result of evaluation is:
  1) For N_CONSTANT nodes: The constant
  2) For N_VARIABLE nodes: The value of the variable
  3) For N_SYSVAR nodes: The value of the system variable
  4) For N_LOCAL_VAR nodes: The value of the user-defined function's argument

  User-defined functions contain their own expr_node tree.  This is
  evaluated with the "locals" parameter set to the values of all
  of the function's arguments.

  Some operators don't evaluate all of their children.  For example,
  the || and && operators always evaluate their leftmost child.  If
  the result is true for ||, or false for &&, then the rightmost child
  is not evaluated because its value is not needed to know the result
  of the operator.

  The built-in functions choose() and iif() also perform this sort of
  short-circuit evaluation.
 */

/*
 * The expression grammar is as follows:
 *
 * EXPR:      OR_EXP                  |
 *            OR_EXP '||' EXPR
 *
 * OR_EXP:    AND_EXP                 |
 *            AND_EXP '&&' OR_EXP
 *
 * AND_EXP:   EQ_EXP                  |
 *            EQ_EXP '==' AND_EXP     |
 *            EQ_EXP '!=' AND_EXP
 *
 * EQ_EXP:    CMP_EXP                 |
 *            CMP_EXP '<' EQ_EXP      |
 *            CMP_EXP '>' EQ_EXP      |
 *            CMP_EXP '<=' EQ_EXP     |
 *            CMP_EXP '<=' EQ_EXP
 *
 * CMP_EXP:   TERM_EXP                |
 *            TERM_EXP '+' CMP_EXP    |
 *            TERM_EXP '-' CMP_EXP
 *
 * TERM_EXP:  FACTOR_EXP              |
 *            FACTOR_EXP '*' TERM_EXP |
 *            FACTOR_EXP '/' TERM_EXP |
 *            FACTOR_EXP '%' TERM_EXP
 *
 * FACTOR_EXP: '-' FACTOR_EXP         |
 *             '!' FACTOR_EXP         |
 *            ATOM
 *
 * ATOM:      '+' ATOM                |
 *            '(' EXPR ')'            |
 *            CONSTANT                |
 *            VAR                     |
 *            FUNCTION_CALL
 */


/* Constants for the "how" arg to compare() */
enum { EQ, GT, LT, GE, LE, NE };

/* Our pool of free expr_node objects, as a linked list, linked by child ptr */
static expr_node *expr_node_free_list = NULL;

#define TOKEN_IS(x) (!strcmp(DBufValue(&ExprBuf), x))
#define TOKEN_ISNOT(x) strcmp(DBufValue(&ExprBuf), x)
#define ISID(c) (isalnum(c) || (c) == '_')

/* Threshold above which to malloc space for function args rather
   than using the stack */
#define STACK_ARGS_MAX 5

/* Maximum parse level before we bail (to avoid SEGV from filling stack)*/
#define MAX_PARSE_LEVEL 2000

static int parse_level_high_water = 0;
#define CHECK_PARSE_LEVEL() do { if (level > parse_level_high_water) { parse_level_high_water = level; if (level > MAX_PARSE_LEVEL) { *r = E_OP_STK_OVER; return NULL; } } } while(0)

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
static expr_node * parse_expression_aux(char const **e, int *r, Var *locals, int level);
static char const *get_operator_name(expr_node *node);

/* This is super-skanky... we keep track of the currently-executing
   user-defined function in a global var */
static UserFunc *CurrentUserFunc = NULL;

/* How many expr_node objects to allocate at a time */
#define ALLOC_CHUNK 256

static char const *
find_end_of_expr(char const *s)
{
    char const *e = s;
    int in_quoted_string = 0;
    int escaped = 0;

    while(*e) {
        if (in_quoted_string) {
            if (escaped) {
                escaped = 0;
                e++;
                continue;
            }
            if (*e == '\\') {
                escaped = 1;
                e++;
                continue;
            }
            if (*e == '"') {
                in_quoted_string = 0;
                e++;
                continue;
            }
            e++;
            continue;
        }
        if (*e == '"') {
            in_quoted_string = 1;
            e++;
            continue;
        }
        if (*e == ']') {
            break;
        }
        e++;
    }
    return e;
}

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
    node->type = N_ERROR;
    node->child = NULL;
    node->sibling = NULL;
    node->num_kids = 0;
    return node;
}

/***************************************************************/
/*                                                             */
/* add_child - add a new child to an existing node             */
/*                                                             */
/* adds "child" as the rightmost child of "parent".  Always    */
/* succeeds, so returns no value.                              */
/*                                                             */
/***************************************************************/
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

/***************************************************************/
/*                                                             */
/* debug_evaluation - print debugging information              */
/*                                                             */
/* This is a helper function for the DB_PRTEXPR (-dx flag)     */
/*                                                             */
/* It prints fmt and following printf-style args, followed     */
/* by " => " and then either an error message for r != OK      */
/* or a value that's the result of evaluation.                 */
/*                                                             */
/***************************************************************/
static void
debug_evaluation(Value *ans, int r, char const *fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);
    vfprintf(ErrFp, fmt, argptr);
    fprintf(ErrFp, " => ");
    if (r != OK) {
        fprintf(ErrFp, "%s\n", GetErr(r));
    } else {
        PrintValue(ans, ErrFp);
        fprintf(ErrFp, "\n");
    }
    va_end(argptr);
}

/***************************************************************/
/*                                                             */
/* debug_evaluation_binop - print debugging information        */
/*                                                             */
/* This is a helper function for the DB_PRTEXPR (-dx flag)     */
/*                                                             */
/* It's called specifically for binary operators.  v1 and v2   */
/* are the operands (a NULL operand indicates one that was     */
/* not evaluated), ans is the result, and r is the error code. */
/*                                                             */
/***************************************************************/
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
        fprintf(ErrFp, "%s\n", GetErr(r));
    } else {
        PrintValue(ans, ErrFp);
        fprintf(ErrFp, "\n");
    }
    va_end(argptr);
}

/***************************************************************/
/*                                                             */
/* debug_evaluation_unop - print debugging information         */
/*                                                             */
/* This is a helper function for the DB_PRTEXPR (-dx flag)     */
/*                                                             */
/* It's called specifically for unary operators.  v1           */
/* is the operand, ans is the result, and r is the error code  */
/*                                                             */
/***************************************************************/
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
        fprintf(ErrFp, "%s\n", GetErr(r));
    } else {
        PrintValue(ans, ErrFp);
        fprintf(ErrFp, "\n");
    }
    va_end(argptr);
}

/***************************************************************/
/*                                                             */
/* get_var - get the value of a variable                       */
/*                                                             */
/* Gets the value of a global variable, with the name taken    */
/* from the appropriate field of `node'                        */
/*                                                             */
/***************************************************************/
static int
get_var(expr_node *node, Value *ans, int *nonconst)
{
    Var *v;
    char const *str;

    if (node->type == N_SHORT_VAR) {
        str = node->u.name;
    } else {
        str = node->u.value.v.str;
    }
    v = FindVar(str, 0);
    if (!v) {
        Eprint("%s: `%s'", GetErr(E_NOSUCH_VAR), str);
        return E_NOSUCH_VAR;
    }
    if (v->nonconstant) {
        nonconst_debug(*nonconst, tr("Global variable `%s' makes expression non-constant"), str);
        *nonconst = 1;
    }
    return CopyValue(ans, &(v->v));
}

/***************************************************************/
/*                                                             */
/* get_sysvar - get the value of a system variable             */
/*                                                             */
/* Gets the value of a system variable, with the name taken    */
/* from the appropriate field of `node'.  The name should not  */
/* incude the leading '$'                                      */
/*                                                             */
/***************************************************************/
static int
get_sysvar(expr_node *node, Value *ans)
{
    if (node->type == N_SHORT_SYSVAR) {
        return GetSysVar(node->u.name, ans);
    } else {
        return GetSysVar(node->u.value.v.str, ans);
    }
}

/***************************************************************/
/*                                                             */
/* eval_builtin - evaluate a builtin function                  */
/*                                                             */
/* node is an expr_node of type N_BUILTIN_FUNC                 */
/* locals is an array of local variables (if any) or NULL      */
/* The result of evaluation is stored in ans                   */
/* If a non-constant function is evaluated, *nonconst is set   */
/* to 1.  The return code is OK if all went well, or an error  */
/* code otherwise.  In case of an error, *ans is not updated   */
/*                                                             */
/* The iif() and choose() functions know how to evaluate       */
/* themselves when passed the expr_node.  All other functions  */
/* use an older API with a func_info object containing the     */
/* evaluated arguments and a spot for the return value.        */
/*                                                             */
/***************************************************************/
static int
eval_builtin(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    func_info info;
    BuiltinFunc *f = node->u.builtin_func;
    expr_node *kid;
    int i, j, r;
    Value stack_args[STACK_ARGS_MAX];

    /* Check that we have the right number of argumens */
    if (node->num_kids < f->minargs) {
        Eprint("%s(): %s", f->name, GetErr(E_2FEW_ARGS));
        return E_2FEW_ARGS;
    }
    if (node->num_kids > f->maxargs && f->maxargs != NO_MAX) {
        Eprint("%s(): %s", f->name, GetErr(E_2MANY_ARGS));
        return E_2MANY_ARGS;
    }

    /* If this is a new-style function that knows about expr_nodes,
       let it evaluate itself */
    if (f->newfunc) {
        return node->u.builtin_func->newfunc(node, locals, ans, nonconst);
    }

    /* It's an old-style function, so we need to simulate the
       old function call API by building up a bundle of evaluated
       arguments */
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

    /* Evaluate each child node and store it as the i'th argument */
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

    /* Mark retval as uninitialized */
    info.retval.type = ERR_TYPE;

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
        /* All went well; copy the result destructively */
        (*ans) = info.retval;

        /* Don't allow retval to be destroyed! */
        info.retval.type = ERR_TYPE;
    }

    /* Debug */
    if (DebugFlag & DB_PRTEXPR) {
        if (r) {
            fprintf(ErrFp, "%s", GetErr(r));
        } else {
            PrintValue(ans, ErrFp);
        }
        fprintf(ErrFp, "\n");
    }
    if (r != OK) {
        Eprint("%s(): %s", f->name, GetErr(r));
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

/***************************************************************/
/*                                                             */
/* debug_enter_userfunc - debugging helper function            */
/*                                                             */
/* This function prints debugging info when a user-defined     */
/* function is invoked.                                        */
/*                                                             */
/***************************************************************/
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
    fprintf(ErrFp, "%s %s(", GetErr(E_ENTER_FUN),  fname);
    for (i=0; i<nargs; i++) {
        if (i) fprintf(ErrFp, ", ");
        PrintValue(&(locals[i]), ErrFp);
    }
    fprintf(ErrFp, ")\n");
}

/***************************************************************/
/*                                                             */
/* debug_exit_userfunc - debugging helper function             */
/*                                                             */
/* This function prints debugging info when a user-defined     */
/* function has been evaluated.                                */
/*                                                             */
/***************************************************************/
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
    fprintf(ErrFp, "%s %s(", GetErr(E_LEAVE_FUN), fname);
    for (i=0; i<nargs; i++) {
        if (i) fprintf(ErrFp, ", ");
        PrintValue(&(locals[i]), ErrFp);
    }
    fprintf(ErrFp, ") => ");
    if (r == OK) {
        PrintValue(ans, ErrFp);
    } else {
        fprintf(ErrFp, "%s", GetErr(r));
    }
    fprintf(ErrFp, "\n");
}

static void
print_placeholders(int n)
{
    int i;
    for (i=0; i<n; i++) {
        if (i > 0) {
            fprintf(ErrFp, ", ");
        }
        fprintf(ErrFp, "?");
    }
}

/***************************************************************/
/*                                                             */
/* eval_userfunc - evaluate a user-defined function            */
/*                                                             */
/* This function sets up a local value array by evaluating     */
/* all of its children, and then evaluates the expr_node       */
/* tree associated with the user-defined function.             */
/*                                                             */
/***************************************************************/
static int
eval_userfunc(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    UserFunc *f;
    UserFunc *previously_executing;

    Value *new_locals = NULL;
    expr_node *kid;
    int i, r, j, pushed;

    /* If we have <= STACK_ARGS_MAX, store them on the stack here */
    Value stack_locals[STACK_ARGS_MAX];

    /* Get the function name */
    char const *fname;
    if (node->type == N_SHORT_USER_FUNC) {
        fname = node->u.name;
    } else {
        fname = node->u.value.v.str;
    }

    /* Find the function */
    f = FindUserFunc(fname);

    /* Bail if function does not exist */
    if (!f) {
        Eprint("%s: `%s'", GetErr(E_UNDEF_FUNC), fname);
        return E_UNDEF_FUNC;
    }

    /* Make sure we have the right number of arguments */
    if (node->num_kids < f->nargs) {
        DBG(fprintf(ErrFp, "%s(", fname); print_placeholders(node->num_kids); fprintf(ErrFp, ") => %s\n", GetErr(E_2FEW_ARGS)));
        Eprint("%s(): %s", f->name, GetErr(E_2FEW_ARGS));
        return E_2FEW_ARGS;
    }
    if (node->num_kids > f->nargs) {
        DBG(fprintf(ErrFp, "%s(", fname); print_placeholders(node->num_kids); fprintf(ErrFp, ") => %s\n", GetErr(E_2MANY_ARGS)));
        Eprint("%s(): %s", f->name, GetErr(E_2MANY_ARGS));
        return E_2MANY_ARGS;
    }

    /* Build up the array of locals */
    if (node->num_kids) {
        if (node->num_kids > STACK_ARGS_MAX) {
            /* Too many args to fit on stack; put on heap */
            new_locals = malloc(node->num_kids * sizeof(Value));
            if (!new_locals) {
                DBG(fprintf(ErrFp, "%s(...) => %s\n", fname, GetErr(E_NO_MEM)));
                return E_NO_MEM;
            }
        } else {
            new_locals = stack_locals;
        }

        /* Evaluate each child node and store in new_locals */
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

    /* Check for deep recursion */
    if (FuncRecursionLevel >= MAX_RECURSION_LEVEL) {
        for (j=0; j<node->num_kids; j++) {
            DestroyValue(new_locals[j]);
        }
        if (new_locals != NULL && new_locals != stack_locals) free(new_locals);
        return E_RECURSIVE;
    }

    /* Set currently-executing function (for debugging) */
    previously_executing = CurrentUserFunc;
    CurrentUserFunc = f;

    FuncRecursionLevel++;

    if (!f->is_constant) {
        nonconst_debug(*nonconst, tr("User function `%s' defined in non-constant context makes expression non-constant"), f->name);
        *nonconst = 1;
    }
    /* Add a call to the call stack for better error messages */
    pushed = push_call(f->filename, f->name, f->lineno, f->lineno_start);

    DBG(debug_enter_userfunc(node, new_locals, f->nargs));

    /* Evaluate the function's expr_node tree */
    r = evaluate_expr_node(f->node, new_locals, ans, nonconst);

    DBG(debug_exit_userfunc(node, ans, r, new_locals, f->nargs));

    if (r != OK) {
        /* We print the error here in order to get the call stack trace */
        Eprint("%s", GetErr(r));
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

/***************************************************************/
/*                                                             */
/* evaluate_expression - evaluate an expression, possibly      */
/*                       with timeout                          */
/*                                                             */
/* See evaluate_expr_node for a description of arguments and   */
/* return value.                                               */
/*                                                             */
/***************************************************************/
int
evaluate_expression(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;

    /* Set up time limits */
    if (ExpressionEvaluationTimeLimit > 0) {
        ExpressionTimeLimitExceeded = 0;
        alarm(ExpressionEvaluationTimeLimit);
    }
    r = evaluate_expr_node(node, locals, ans, nonconst);
    if (ExpressionEvaluationTimeLimit > 0) {
        alarm(0);
        ExpressionTimeLimitExceeded = 0;
    }
    return r;
}

static int CopyShortStr(Value *ans, expr_node *node)
{
    size_t len = strlen(node->u.name);
    ans->v.str = malloc(len+1);
    if (!ans->v.str) {
        ans->type = ERR_TYPE;
        return E_NO_MEM;
    }
    strcpy(ans->v.str, node->u.name);
    ans->type = STR_TYPE;
    return OK;
}

/***************************************************************/
/*                                                             */
/* evaluate_expr_node - evaluate an expression                 */
/*                                                             */
/* Arguments:                                                  */
/*   node     - the expr_node to evaluate                      */
/*   locals   - an array of arguments to a user-defined        */
/*              function.  NULL if we're not in a function     */
/*   ans      - pointer to a Value object that will hold       */
/*              the result of an evaluation                    */
/*   nonconst - pointer to an integer that will be set to 1    */
/*              if a non-constant object or function is        */
/*              evaluated                                      */
/* Returns:                                                    */
/*   OK if all goes well; a non-zero error code on failure.    */
/*   On failure, *ans is not updated.                          */
/*                                                             */
/***************************************************************/
int
evaluate_expr_node(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;

    if (ExpressionEvaluationDisabled) {
        return E_EXPR_DISABLED;
    }

    if (ExpressionTimeLimitExceeded) {
        ExpressionTimeLimitExceeded = 0;
        return E_TIME_EXCEEDED;
    }
    if (!node) {
        return E_SWERR;
    }
    switch(node->type) {
    case N_FREE:
    case N_ERROR:
        ans->type = ERR_TYPE;
        return E_SWERR;

    case N_SHORT_STR:
        return CopyShortStr(ans, node);

    case N_CONSTANT:
        /* Constant node?  Just return a copy of the constant */
        return CopyValue(ans, &(node->u.value));

    case N_SHORT_VAR:
        r = get_var(node, ans, nonconst);
        DBG(debug_evaluation(ans, r, "%s", node->u.name));
        return r;

    case N_VARIABLE:
        r = get_var(node, ans, nonconst);
        DBG(debug_evaluation(ans, r, "%s", node->u.value.v.str));
        return r;

    case N_LOCAL_VAR:
        /* User-defined function argument? Copy the value */
        r = CopyValue(ans, &(locals[node->u.arg]));
        DBG(debug_evaluation(ans, r, "%s", CurrentUserFunc->args[node->u.arg]));
        return r;

    case N_SHORT_SYSVAR:
        /* System var?  Return it and note non-constant expression */
        nonconst_debug(*nonconst, tr("System variable `$%s' makes expression non-constant"), node->u.name);
        *nonconst = 1;
        r = get_sysvar(node, ans);
        DBG(debug_evaluation(ans, r, "$%s", node->u.name));
        return r;

    case N_SYSVAR:
        /* System var?  Return it and note non-constant expression */
        nonconst_debug(*nonconst, tr("System variable `$%s' makes expression non-constant"), node->u.value.v.str);
        *nonconst = 1;
        r = get_sysvar(node, ans);
        DBG(debug_evaluation(ans, r, "$%s", node->u.value.v.str));
        return r;

    case N_BUILTIN_FUNC:
        /* Built-in function?  Evaluate and note non-constant where applicable */
        if (!node->u.builtin_func->is_constant) {
            nonconst_debug(*nonconst, tr("Non-constant built-in function `%s' makes expression non-constant"), node->u.builtin_func->name);
            *nonconst = 1;
        }
        return eval_builtin(node, locals, ans, nonconst);

    case N_USER_FUNC:
    case N_SHORT_USER_FUNC:
        /* User-defined function?  Evaluate it */
        return eval_userfunc(node, locals, ans, nonconst);

    case N_OPERATOR:
        /* Operator?  Evaluate it */
        r = node->u.operator_func(node, locals, ans, nonconst);
        if (r != OK) {
            Eprint("`%s': %s", get_operator_name(node), GetErr(r));
        }
        return r;
    }

    /* Can't happen */
    return E_SWERR;
}

/***************************************************************/
/*                                                             */
/* how_to_op - convert a symbolic comparison constant to name  */
/*                                                             */
/* Converts EQ, NE, etc to "==", "!=", etc for debugging       */
/*                                                             */
/***************************************************************/
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

/***************************************************************/
/*                                                             */
/* compare - evaluate a comparison operator.                   */
/*                                                             */
/* In addition to the usual arguments, "how" specifies         */
/* specifically which comparison operator we're evaluating.    */
/*                                                             */
/***************************************************************/
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

/***************************************************************/
/*                                                             */
/* compare_eq - evaluate the "==" operator                     */
/*                                                             */
/***************************************************************/
static int compare_eq(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, EQ);
}

/***************************************************************/
/*                                                             */
/* compare_ne evaluate the "!=" operator                       */
/*                                                             */
/***************************************************************/
static int compare_ne(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, NE);
}

/***************************************************************/
/*                                                             */
/* compare_le - evaluate the "<=" operator                     */
/*                                                             */
/***************************************************************/
static int compare_le(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, LE);
}

/***************************************************************/
/*                                                             */
/* compare_ge - evaluate the ">=" operator                     */
/*                                                             */
/***************************************************************/
static int compare_ge(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, GE);
}

/***************************************************************/
/*                                                             */
/* compare_lt - evaluate the "<" operator                      */
/*                                                             */
/***************************************************************/
static int compare_lt(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, LT);
}

/***************************************************************/
/*                                                             */
/* compare_gt - evaluate the ">" operator                      */
/*                                                             */
/***************************************************************/
static int compare_gt(expr_node *node, Value *locals, Value *ans, int *nonconst) {
    return compare(node, locals, ans, nonconst, GT);
}


/***************************************************************/
/*                                                             */
/* add - evaluate the "+" operator                             */
/*                                                             */
/***************************************************************/
static int add(expr_node *node, Value *locals, Value *ans, int *nonconst)
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
        /* Skanky... copy the values shallowly for debug */
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
        DestroyValue(v1);
        DestroyValue(v2);
        return OK;
    }

    /* Don't handle other types yet */
    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "+"));
    DestroyValue(v1);
    DestroyValue(v2);
    return E_BAD_TYPE;
}

/***************************************************************/
/*                                                             */
/* subtract - evaluate the binary "-" operator                 */
/*                                                             */
/***************************************************************/
static int subtract(expr_node *node, Value *locals, Value *ans, int *nonconst)
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

/***************************************************************/
/*                                                             */
/* multiply - evaluate the "*" operator                        */
/*                                                             */
/***************************************************************/
static int multiply(expr_node *node, Value *locals, Value *ans, int *nonconst)
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

/***************************************************************/
/*                                                             */
/* divide_or_mod - evaluate the "/" or "%" operator            */
/*                                                             */
/***************************************************************/
static int divide_or_mod(expr_node *node, Value *locals, Value *ans, int *nonconst, char op)
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
            DBG(debug_evaluation_binop(ans, E_DIV_ZERO, &v1, &v2, "%c", op));
            return E_DIV_ZERO;
        }
        /* This is the only way it can overflow */
        if (v2.v.val == -1 && v1.v.val == INT_MIN) {
            DBG(debug_evaluation_binop(ans, E_2HIGH, &v1, &v2, "%c", op));
            return E_2HIGH;
        }
        *ans = v1;
        if (op == '/') {
            ans->v.val /= v2.v.val;
        } else {
            ans->v.val %= v2.v.val;
        }
        DBG(debug_evaluation_binop(ans, OK, &v1, &v2, "%c", op));
        return OK;
    }
    DBG(debug_evaluation_binop(ans, E_BAD_TYPE, &v1, &v2, "%c", op));
    DestroyValue(v1);
    DestroyValue(v2);
    return E_BAD_TYPE;
}

/***************************************************************/
/*                                                             */
/* do_mod - evaluate the "%" operator                          */
/*                                                             */
/***************************************************************/
static int do_mod(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    return divide_or_mod(node, locals, ans, nonconst, '%');
}

/***************************************************************/
/*                                                             */
/* divide - evaluate the "/" operator                          */
/*                                                             */
/***************************************************************/
static int divide(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    return divide_or_mod(node, locals, ans, nonconst, '/');
}

/***************************************************************/
/*                                                             */
/* logical_not - evaluate the unary "!" operator               */
/*                                                             */
/***************************************************************/
static int logical_not(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    Value v1;

    r = evaluate_expr_node(node->child, locals, &v1, nonconst);
    if (r != OK) return r;
    ans->type = INT_TYPE;
    ans->v.val = !truthy(&v1);
    DBG(debug_evaluation_unop(ans, OK, &v1, "!"));
    DestroyValue(v1);
    return OK;
}

/***************************************************************/
/*                                                             */
/* unary_minus - evaluate the unary "-" operator               */
/*                                                             */
/***************************************************************/
static int unary_minus(expr_node *node, Value *locals, Value *ans, int *nonconst)
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
    if (v1.v.val == INT_MIN) {
        DBG(debug_evaluation_unop(ans, E_2LOW, &v1, "-"));
        DestroyValue(v1);
        return E_2LOW;
    }
    ans->type = INT_TYPE;
    ans->v.val = -(v1.v.val);
    DBG(debug_evaluation_unop(ans, OK, &v1, "-"));
    return OK;
}

/***************************************************************/
/*                                                             */
/* logical_binop - evaluate the short-circuit || or && ops     */
/*                                                             */
/***************************************************************/
static int logical_binop(expr_node *node, Value *locals, Value *ans, int *nonconst, int is_and)
{
    Value v;
    char const *opname = (is_and) ? "&&" : "||";

    /* Evaluate first arg */
    int r = evaluate_expr_node(node->child, locals, &v, nonconst);

    /* Bail on error */
    if (r != OK) return r;

    if (is_and) {
        /* If first arg is false, return it */
        if (!truthy(&v)) {
            *ans = v;
            DBG(debug_evaluation_binop(ans, OK, &v, NULL, opname));
            return OK;
        }
    } else {
        /* If first arg is true, return it */
        if (truthy(&v)) {
            *ans = v;
            DBG(debug_evaluation_binop(ans, OK, &v, NULL, opname));
            return OK;
        }
    }

    /* Otherwise, evaluate and return second arg */
    r = evaluate_expr_node(node->child->sibling, locals, ans, nonconst);
    DBG(debug_evaluation_binop(ans, r, &v, ans, opname));
    DestroyValue(v);
    return r;
}

/***************************************************************/
/*                                                             */
/* logical_or - evaluate the short-circuit || operator         */
/*                                                             */
/***************************************************************/
static int logical_or(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    return logical_binop(node, locals, ans, nonconst, 0);
}

/***************************************************************/
/*                                                             */
/* logical_and - evaluate the short-circuit && operator        */
/*                                                             */
/***************************************************************/
static int logical_and(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    return logical_binop(node, locals, ans, nonconst, 1);
}

/***************************************************************/
/*                                                             */
/*  parse_expr_token                                           */
/*                                                             */
/*  Read a token.                                              */
/*                                                             */
/***************************************************************/
static int parse_expr_token(DynamicBuffer *buf, char const **in)
{

    char c;
    char c2;
    char hexbuf[3];

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
        } else {
            Eprint("%s `%c' (%s `%c%c'?)", GetErr(E_PARSE_ERR), c, tr("did you mean"), c, c);
            return E_PARSE_ERR;
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
                case 'x':
                    c2 = *(*in + 1);
                    if (!isxdigit(c2)) {
                        r = DBufPutc(buf, **in);
                        break;
                    }
                    hexbuf[0] = c2;
                    hexbuf[1] = 0;
                    (*in)++;
                    c2 = *(*in + 1);
                    if (isxdigit(c2)) {
                        hexbuf[1] = c2;
                        hexbuf[2] = 0;
                        (*in)++;
                    }
                    c2 = (int) strtol(hexbuf, NULL, 16);
                    if (!c2) {
                        Eprint(tr("\\x00 is not a valid escape sequence"));
                        r = E_PARSE_ERR;
                    } else {
                        r = DBufPutc(buf, c2);
                    }
                    break;
                default:
                    r = DBufPutc(buf, **in);
                    break;
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
        if (!c) {
            Eprint("%s", GetErr(E_EOLN));
            return E_EOLN;
        }
        Eprint("%s `%c'", GetErr(E_ILLEGAL_CHAR), c);
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
    return parse_expr_token(buf, &in);
}

/***************************************************************/
/*                                                             */
/*  free_expr_tree                                             */
/*                                                             */
/*  Recursively free the expr_node tree rooted at node         */
/*  Always returns NULL                                        */
/*                                                             */
/***************************************************************/
expr_node * free_expr_tree(expr_node *node)
{
    if (node && (node->type != N_FREE)) {
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

/***************************************************************/
/*                                                             */
/*  set_long_name - set a long name in an expr_node            */
/*                                                             */
/*  Set the name field in an expr_node that's too long to fit  */
/*  in u.name -- instead, set in u.value.v.str                 */
/*                                                             */
/***************************************************************/
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

/***************************************************************/
/*                                                             */
/*  parse_function_call - parse a function call                */
/*                                                             */
/*  Starting from *e, parse a function call and return the     */
/*  parsed expr_node tree                                      */
/*                                                             */
/*  All parsing functions have the following arguments and     */
/*  return value:                                              */
/*                                                             */
/*  e - the current parse pointer.  *e points to the           */
/*  character we're readin, and *e is updated as we parse      */
/*                                                             */
/*  r - holds the return code.  Set to OK if all is well       */
/*  or a non-zero error code on error                          */
/*                                                             */
/*  locals - an array of Vars representing named arguments     */
/*  for a user-defined function                                */
/*                                                             */
/*  Returns an expr_node on success, NULL on failure.          */
/*                                                             */
/***************************************************************/
static expr_node * parse_function_call(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *arg;
    char *s;
    char const *ptr;
    CHECK_PARSE_LEVEL();

    node = alloc_expr_node(r);
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
        arg = parse_expression_aux(e, r, locals, level+1);
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
                Eprint("%s `)'", GetErr(E_PARSE_ERR));
                *r = E_PARSE_ERR;
                return free_expr_tree(node);
            }
        }
    }
    ptr = *e;
    if (TOKEN_IS(")")) {
        *r = GET_TOKEN();
        if (*r != OK) {
            return free_expr_tree(node);
        }
    }
    /* Check args for builtin funcs */
    if (node->type == N_BUILTIN_FUNC) {
        f = node->u.builtin_func;
        if (node->num_kids < f->minargs) {
            *e = ptr;
            *r = E_2FEW_ARGS;
        }
        if (node->num_kids > f->maxargs && f->maxargs != NO_MAX) {
            *e = ptr;
            *r = E_2MANY_ARGS;
        }
    }
    if (*r != OK) {
        if (node->type == N_BUILTIN_FUNC) {
            f = node->u.builtin_func;
            Eprint("%s: %s", f->name, GetErr(*r));
        }
        return free_expr_tree(node);
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* set_constant_value - Given a constant value token in        */
/* ExprBuf, parse ExprBuf and set atom->u.value appropriately  */
/*                                                             */
/***************************************************************/
static int set_constant_value(expr_node *atom)
{
    int dse, tim, val, prev_val, h, m, ampm, r;
    size_t len;
    char const *s = DBufValue(&ExprBuf);
    atom->u.value.type = ERR_TYPE;
    atom->type = N_CONSTANT;

    if (!*s) {
        Eprint("%s", GetErr(E_EOLN));
        return E_EOLN;
    }
    ampm = 0;
    if (*s == '\"') { /* It's a literal string "*/
        len = strlen(s)-1;
        if (len <= SHORT_NAME_BUF) {
            atom->type = N_SHORT_STR;
            strncpy(atom->u.name, s+1, len-1);
            atom->u.name[len-1] = 0;
            return OK;
        }
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
        if ((r=ParseLiteralDateOrTime(&s, &dse, &tim)) != 0) {
            Eprint("%s: %s", GetErr(r), DBufValue(&ExprBuf));
            return r;
        }
        if (*s != '\'') {
            if (dse != NO_DATE) {
                Eprint("%s: %s", GetErr(E_BAD_DATE), DBufValue(&ExprBuf));
                return E_BAD_DATE;
            } else {
                Eprint("%s: %s", GetErr(E_BAD_TIME), DBufValue(&ExprBuf));
                return E_BAD_TIME;
            }
        }
        if (tim == NO_TIME) {
            atom->u.value.type = DATE_TYPE;
            atom->u.value.v.val = dse;
        } else if (dse == NO_DATE) {
            atom->u.value.type = TIME_TYPE;
            atom->u.value.v.val = tim;
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
            if (!isdigit(*s)) {
                Eprint("%s: `%s'", GetErr(E_BAD_TIME), DBufValue(&ExprBuf));
                return E_BAD_TIME;
            }
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
            if (*s || h>23 || m>59) {
                Eprint("%s: `%s'", GetErr(E_BAD_TIME), DBufValue(&ExprBuf));
                return E_BAD_TIME;
            }
            if (ampm) {
                if (h < 1 || h > 12) {
                    Eprint("%s: `%s'", GetErr(E_BAD_TIME), DBufValue(&ExprBuf));
                    return E_BAD_TIME;
                }
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
        if (*s) {
            Eprint("%s: `%s'", GetErr(E_BAD_NUMBER), DBufValue(&ExprBuf));
            return E_BAD_NUMBER;
        }
        atom->u.value.type = INT_TYPE;
        atom->u.value.v.val = val;
        return OK;
    }
    atom->u.value.type = ERR_TYPE;
    Eprint("`%s': %s", DBufValue(&ExprBuf), GetErr(E_ILLEGAL_CHAR));
    return E_ILLEGAL_CHAR;
}

/***************************************************************/
/*                                                             */
/* make_atom - create a constant, variable, system variable    */
/* or local variable node.                                     */
/*                                                             */
/***************************************************************/
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
            v = v->link.next;
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
            Eprint("%s: `%s'", GetErr(E_NOSUCH_VAR), s);
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
    if (r != OK) {
        atom->type = N_ERROR;
    }
    return r;
}

/***************************************************************/
/*                                                             */
/* Parse an atom.                                              */
/*                                                             */
/* ATOM:      '(' EXPR ')'            |                        */
/*            CONSTANT                |                        */
/*            VAR                     |                        */
/*            FUNCTION_CALL                                    */
/*                                                             */
/***************************************************************/
static expr_node *parse_atom(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    char const *s;
    CHECK_PARSE_LEVEL();
    *r = PEEK_TOKEN();
    if (*r != OK) return  NULL;

    if (TOKEN_IS("(")) {
        /* Parenthesiszed expession:  '('   EXPR   ')' */

        /* Pull off the peeked token */
        *r = GET_TOKEN();
        if (*r != OK) {
            return NULL;
        }
        node = parse_expression_aux(e, r, locals, level+1);
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
    if (!*s) {
        Eprint("%s", GetErr(E_EOLN));
        *r = E_EOLN;
        return NULL;
    }
    if (!ISID(*s) &&
        *s != '$' &&
        *s != '"' &&
        *s != '\'') {
        Eprint("%s `%c'", GetErr(E_ILLEGAL_CHAR), *s);
        *r = E_ILLEGAL_CHAR;
        return NULL;
    }

    /* Is it a function call? */
    if (*(s + DBufLen(&ExprBuf) - 1) == '(') {
        return parse_function_call(e, r, locals, level+1);
    }

    /* It's a constant or a variable reference */
    char const *olds = *e;
    *r = GET_TOKEN();
    if (*r != OK) return NULL;
    node = alloc_expr_node(r);
    if (!node) {
        return NULL;
    }
    *r = make_atom(node, locals);
    if (*r != OK) {
        /* Preserve location for error position when we print ^-- here */
        *e = olds;
        return free_expr_tree(node);
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* parse_factor - parse a factor                               */
/*                                                             */
/* FACTOR_EXP: '-' FACTOR_EXP         |                        */
/*             '!' FACTOR_EXP         |                        */
/*             '+' FACTOR_EXP                                  */
/*            ATOM                                             */
/*                                                             */
/***************************************************************/
static expr_node *parse_factor(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *factor_node;
    char op;
    CHECK_PARSE_LEVEL();
    *r = PEEK_TOKEN();
    if (*r != OK) {
        return NULL;
    }
    if (TOKEN_IS("!") || TOKEN_IS("-") || TOKEN_IS("+")) {
        if (TOKEN_IS("!")) {
            op = '!';
        } else if (TOKEN_IS("-")) {
            op = '-';
        } else {
            op = '+';
        }
        /* Pull off the peeked token */
        GET_TOKEN();
        node = parse_factor(e, r, locals, level+1);
        if (*r != OK) {
            return NULL;
        }

        /* Ignore unary plus operator */
        if (op == '+') {
            return node;
        }

        /* Optimize '-' or '!' followed by integer constant */
        if (node->type == N_CONSTANT &&node->u.value.type == INT_TYPE) {
            if (op == '-') {
                node->u.value.v.val = - node->u.value.v.val;
            } else {
                node->u.value.v.val = ! node->u.value.v.val;
            }
            return node;
        }
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
    return parse_atom(e, r, locals, level+1);
}

/***************************************************************/
/*                                                             */
/* parse_term - parse a term                                   */
/*                                                             */
/* TERM_EXP:  FACTOR_EXP              |                        */
/*            FACTOR_EXP '*' TERM_EXP |                        */
/*            FACTOR_EXP '/' TERM_EXP |                        */
/*            FACTOR_EXP '%' TERM_EXP                          */
/*                                                             */
/***************************************************************/
static expr_node *parse_term_expr(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *term_node;
    CHECK_PARSE_LEVEL();

    node = parse_factor(e, r, locals, level+1);
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
        node = parse_factor(e, r, locals, level+1);
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

/***************************************************************/
/*                                                             */
/* parse_cmp_expr - parse a cmp_expr                           */
/*                                                             */
/* CMP_EXP:   TERM_EXP                |                        */
/*            TERM_EXP '+' CMP_EXP    |                        */
/*            TERM_EXP '-' CMP_EXP                             */
/*                                                             */
/***************************************************************/
static expr_node *parse_cmp_expr(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *cmp_node;
    CHECK_PARSE_LEVEL();

    node = parse_term_expr(e, r, locals, level+1);
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
        node = parse_term_expr(e, r, locals, level+1);
        if (*r != OK) {
            return free_expr_tree(cmp_node);
        }
        add_child(cmp_node, node);
        node = cmp_node;
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* parse_eq_expr - parse an eq_expr                            */
/*                                                             */
/* EQ_EXP:    CMP_EXP                 |                        */
/*            CMP_EXP '<' EQ_EXP      |                        */
/*            CMP_EXP '>' EQ_EXP      |                        */
/*            CMP_EXP '<=' EQ_EXP     |                        */
/*            CMP_EXP '<=' EQ_EXP                              */
/*                                                             */
/***************************************************************/
static expr_node *parse_eq_expr(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *eq_node;
    CHECK_PARSE_LEVEL();

    node = parse_cmp_expr(e, r, locals, level+1);
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
        node = parse_cmp_expr(e, r, locals, level+1);
        if (*r != OK) {
            free_expr_tree(eq_node);
            return free_expr_tree(node);
        }
        add_child(eq_node, node);
        node = eq_node;
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* parse_and_expr - parse an and_expr                          */
/*                                                             */
/* AND_EXP:   EQ_EXP                  |                        */
/*            EQ_EXP '==' AND_EXP     |                        */
/*            EQ_EXP '!=' AND_EXP                              */
/*                                                             */
/***************************************************************/
static expr_node *parse_and_expr(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *and_node;
    CHECK_PARSE_LEVEL();

    node = parse_eq_expr(e, r, locals, level+1);
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
        node = parse_eq_expr(e, r, locals, level+1);
        if (*r != OK) {
            return free_expr_tree(and_node);
        }
        add_child(and_node, node);
        node = and_node;
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* parse_or_expr - parse an or_expr                            */
/*                                                             */
/* OR_EXP:    AND_EXP                 |                        */
/*            AND_EXP '&&' OR_EXP                              */
/*                                                             */
/***************************************************************/
static expr_node *parse_or_expr(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *logand_node;
    CHECK_PARSE_LEVEL();

    node = parse_and_expr(e, r, locals, level+1);

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
        node = parse_and_expr(e, r, locals, level+1);
        if (*r != OK) {
            free_expr_tree(logand_node);
            return free_expr_tree(node);
        }
        add_child(logand_node, node);
        node = logand_node;
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* parse_expression_aux - parse an EXPR                        */
/*                                                             */
/* EXPR:      OR_EXP                  |                        */
/*            OR_EXP '||' EXPR                                 */
/*                                                             */
/***************************************************************/
static expr_node *parse_expression_aux(char const **e, int *r, Var *locals, int level)
{
    expr_node *node;
    expr_node *logor_node;
    CHECK_PARSE_LEVEL();

    node = parse_or_expr(e, r, locals, level+1);

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
        node = parse_or_expr(e, r, locals, level+1);
        if (*r != OK) {
            free_expr_tree(logor_node);
            return free_expr_tree(node);
        }
        add_child(logor_node, node);
        node = logor_node;
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* parse_expression - top-level expression-parsing function    */
/*                                                             */
/* *e points to string being parsed; *e will be updates        */
/* r points to a location for the return code (OK or an error) */
/* locals is an array of local function arguments, if any      */
/*                                                             */
/* Returns a parsed expr_node or NULL on error                 */
/*                                                             */
/***************************************************************/
expr_node *parse_expression(char const **e, int *r, Var *locals)
{
    char const *orig = *e;
    char const *o2 = *e;
    char const *end_of_expr;
    if (ExpressionEvaluationDisabled) {
        *r = E_EXPR_DISABLED;
        return NULL;
    }

    expr_node *node = parse_expression_aux(e, r, locals, 0);
    if (DebugFlag & DB_PARSE_EXPR) {
        fprintf(ErrFp, "Parsed expression: ");
        while (*orig && orig != *e) {
            putc(*orig, ErrFp);
            orig++;
        }
        putc('\n', ErrFp);
        if (*r != OK) {
            fprintf(ErrFp, "  => Error: %s\n", GetErr(*r));
        } else {
            fprintf(ErrFp, "  => ");
            print_expr_tree(node, ErrFp);
            fprintf(ErrFp, "\n");
        }
        if (**e && (**e != ']')) {
            fprintf(ErrFp, "  Unparsed: %s\n", *e);
        }
    }
    if (*r == E_EXPECT_COMMA     ||
        *r == E_MISS_RIGHT_PAREN ||
        *r == E_EXPECTING_EOL    ||
        *r == E_2MANY_ARGS       ||
        *r == E_2FEW_ARGS        ||
        *r == E_PARSE_ERR        ||
        *r == E_EOLN             ||
        *r == E_BAD_NUMBER       ||
        *r == E_BAD_DATE         ||
        *r == E_BAD_TIME         ||
        *r == E_ILLEGAL_CHAR) {
        end_of_expr = find_end_of_expr(orig);
        while (**e && isempty(**e)) {
            (*e)++;
        }
        while (*orig && ((orig < end_of_expr) || (orig <= *e))) {
            if (*orig == '\n') {
                fprintf(ErrFp, " ");
            } else {
                fprintf(ErrFp, "%c", *orig);
            }
            orig++;
        }
        fprintf(ErrFp, "\n");
        orig = o2;
        while (*orig && (orig < *e || isspace(*orig))) {
            orig++;
            fprintf(ErrFp, " ");
        }
        fprintf(ErrFp, "^-- %s\n", tr("here"));
    }
    return node;
}


/***************************************************************/
/*                                                             */
/* print_kids - print all of this node's children to a file,   */
/* recursively.  Used for debugging.                           */
/*                                                             */
/***************************************************************/
static void print_kids(expr_node *node, FILE *fp)
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

/***************************************************************/
/*                                                             */
/* print_expr_tree - print the entire expression tree to a     */
/* file.  Used for debugging (the "-ds" flag.)                 */
/*                                                             */
/***************************************************************/
void print_expr_tree(expr_node *node, FILE *fp)
{
    if (!node) {
        return;
    }
    switch(node->type) {
    case N_CONSTANT:
        PrintValue(&(node->u.value), fp);
        return;
    case N_SHORT_STR:
        fprintf(fp, "\"%s\"", node->u.name);
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
        fprintf(fp, "(%c%s",
                toupper(*(node->u.builtin_func->name)),
                node->u.builtin_func->name+1);
        if (node->child) fprintf(fp, " ");
        print_kids(node, fp);
        fprintf(fp, ")");
        return;
    case N_SHORT_USER_FUNC:
        fprintf(fp, "(%s", node->u.name);
        if (node->child) fprintf(fp, " ");
        print_kids(node, fp);
        fprintf(fp, ")");
        return;
    case N_USER_FUNC:
        fprintf(fp, "(%s", node->u.value.v.str);
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

/***************************************************************/
/*                                                             */
/* get_operator_name - given an expr_node of type N_OPERATOR,  */
/* return a string representation of the operator.             */
/*                                                             */
/***************************************************************/
static char const *get_operator_name(expr_node *node)
{
    int (*f)(expr_node *node, Value *locals, Value *ans, int *nonconst) = node->u.operator_func;
    if (f == logical_not) return "!";
    else if (f == unary_minus) return "-";
    else if (f == multiply) return "*";
    else if (f == divide) return "/";
    else if (f == do_mod) return "%";
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
/*  EvalExpr - parse and evaluate an expression.               */
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
    r = evaluate_expression(n, NULL, v, &nonconst);

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
/*  Copy a value.  If value is a string, strdups the string.   */
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

/***************************************************************/
/*                                                             */
/*  ParseLiteralTime - parse a literal time like 11:15 or      */
/*  4:00PM                                                     */
/*                                                             */
/***************************************************************/
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
/*  ParseLiteralDateOrTime                                           */
/*                                                             */
/*  Parse a literal date or datetime.  Return result in dse    */
/*  and tim; update s.                                         */
/*                                                             */
/***************************************************************/
int ParseLiteralDateOrTime(char const **s, int *dse, int *tim)
{
    int y, m, d;
    int r;

    char const *orig_s = *s;

    y=0; m=0; d=0;

    *tim = NO_TIME;
    *dse = NO_DATE;
    if (!isdigit(**s)) return E_BAD_DATE;
    while (isdigit(**s)) {
        y *= 10;
        y += *(*s)++ - '0';
    }
    if (**s == ':' || **s == '.' || **s == TimeSep) {
        *s = orig_s;
        return ParseLiteralTime(s, tim);
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
            if (ParseLiteralDateOrTime(&s, &i, &m)) return E_CANT_COERCE;
            if (i == NO_DATE) return E_CANT_COERCE;
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
        case INT_TYPE: snprintf(coerce_buf, sizeof(coerce_buf), "%d", v->v.val); break;
        case TIME_TYPE: snprintf(coerce_buf, sizeof(coerce_buf), "%02d%c%02d", v->v.val / 60,
                               TimeSep, v->v.val % 60);
        break;
        case DATE_TYPE: FromDSE(v->v.val, &y, &m, &d);
            snprintf(coerce_buf, sizeof(coerce_buf), "%04d%c%02d%c%02d",
                    y, DateSep, m+1, DateSep, d);
            break;
        case DATETIME_TYPE:
            i = v->v.val / MINUTES_PER_DAY;
            FromDSE(i, &y, &m, &d);
            k = v->v.val % MINUTES_PER_DAY;
            h = k / 60;
            i = k % 60;
            snprintf(coerce_buf, sizeof(coerce_buf), "%04d%c%02d%c%02d%c%02d%c%02d",
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
            if (ParseLiteralDateOrTime(&s, &i, &m)) return E_CANT_COERCE;
            if (i == NO_DATE) return E_CANT_COERCE;
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
            i=0; /* Avoid compiler warning */
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

/***************************************************************/
/*                                                             */
/* print_expr_nodes_stats - print statistics about expr_node   */
/* allocation.                                                 */
/*                                                             */
/* This is a debugging routine that prints data about          */
/* expr_node allocation on program exit                        */
/*                                                             */
/***************************************************************/
void print_expr_nodes_stats(void)
{
    fprintf(ErrFp, " Expression nodes allocated: %d\n", ExprNodesAllocated);
    fprintf(ErrFp, "Expression nodes high-water: %d\n", ExprNodesHighWater);
    fprintf(ErrFp, "    Expression nodes leaked: %d\n", ExprNodesUsed);
    fprintf(ErrFp, "     Parse level high-water: %d\n", parse_level_high_water);
}

/* Return 1 if a value is "true" for its type, 0 if "false" */
int truthy(Value const *v)
{
    if (v->type == STR_TYPE) {
        if (v->v.str && *(v->v.str)) {
            return 1;
        } else {
            return 0;
        }
    }

    return (v->v.val != 0);
}
