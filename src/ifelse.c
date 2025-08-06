/***************************************************************/
/*                                                             */
/*  IFELSE.C                                                   */
/*                                                             */
/*  Logic for tracking the state of the IF... ELSE... ENDIF    */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/
#include "config.h"
#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"

/* Allow up to 64 levels if IF...ELSE nesting across all files */
#define IF_NEST 64

/* Points to the next unused IF entry structure */
static int if_pointer = 0;

/* The base pointer for the current file */
static int base_pointer = 0;

/* True if a "RETURN" statement was encountered in current file */
static int return_encountered = 0;

/*
 * The current state of the IF...ELSE...ENDIF context is stored in
 * an array of "ifentry" objects, from the outermost to the
 * innermost.
 *
 * The if_true element is true of the IF expression was true; false
 * otherwise.
 *
 * The before_else element is true if the current line is after
 * the IF but before any ELSE; false otherwise.
 *
 * The was_constant element is true if the IF expression was constant.
 *
 * Each time a new file is INCLUDed, we reset the base pointer to the
 * current location of the if_pointer.
 */

typedef struct ifentry_struct {
    int lineno;
    unsigned char if_true;
    unsigned char before_else;
    unsigned char was_constant;
} ifentry;

static ifentry IfArray[IF_NEST];

/***************************************************************/
/*                                                             */
/* push_if - push an if entry onto the stack.                  */
/*                                                             */
/***************************************************************/
int
push_if(int is_true, int was_constant)
{
    if (if_pointer >= IF_NEST) {
        return E_NESTED_IF;
    }
    IfArray[if_pointer].if_true = is_true;
    IfArray[if_pointer].before_else = 1;
    IfArray[if_pointer].was_constant = was_constant;
    IfArray[if_pointer].lineno = LineNo;
    if_pointer++;
    return OK;
}

/***************************************************************/
/*                                                             */
/* if_stack_full - returns OK or E_NESTED_IF                   */
/*                                                             */
/***************************************************************/
int
if_stack_full(void)
{
    if (if_pointer >= IF_NEST) {
        return E_NESTED_IF;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/* encounter_else - note that the most-recently-pushed IF      */
/*                  has encountered an ELSE                    */
/*                                                             */
/***************************************************************/
int
encounter_else(void)
{
    if (if_pointer <= base_pointer) {
        return E_ELSE_NO_IF;
    }
    if (!IfArray[if_pointer-1].before_else) {
        return E_ELSE_NO_IF;
    }

    IfArray[if_pointer-1].before_else = 0;
    return OK;
}

/***************************************************************/
/*                                                             */
/* DoReturn - handle the RETURN command                        */
/*                                                             */
/***************************************************************/
int
DoReturn(ParsePtr p)
{
    int r = VerifyEoln(p);
    return_encountered = 1;
    return r;
}

/***************************************************************/
/*                                                             */
/* encounter_endif - note that the most-recently-pushed IF     */
/*                  has encountered an ENDIF                   */
/*                                                             */
/***************************************************************/
int
encounter_endif(void)
{
    if (if_pointer <= base_pointer) {
        return E_ENDIF_NO_IF;
    }
    if_pointer--;
    return OK;
}

/***************************************************************/
/*                                                             */
/* get_base_if_pointer - get the current base_pointer          */
/*                                                             */
/***************************************************************/
int
get_base_if_pointer(void)
{
    return base_pointer;
}

/***************************************************************/
/*                                                             */
/* get_if_pointer - get the current if_pointer                 */
/*                                                             */
/***************************************************************/
int
get_if_pointer(void)
{
    return if_pointer;
}

/***************************************************************/
/*                                                             */
/* set_base_if_pointer - set the base_pointer to n             */
/*                                                             */
/***************************************************************/
void
set_base_if_pointer(int n)
{
    base_pointer = n;
}

/***************************************************************/
/*                                                             */
/* should_ignore_line - return 1 if current line should be     */
/*                      ignored.                               */
/*                                                             */
/***************************************************************/
int
should_ignore_line(void)
{
    int i;

    if (return_encountered) {
        return 1;
    }
    for (i=base_pointer; i<if_pointer; i++) {
        if (( IfArray[i].if_true && !IfArray[i].before_else) ||
            (!IfArray[i].if_true &&  IfArray[i].before_else)) {
            return 1;
        }
    }
    return 0;
}

/***************************************************************/
/*                                                             */
/* in_constant_context - return true if we're in a "constant"  */
/*                       assignment context.                   */
/*                                                             */
/***************************************************************/
int
in_constant_context(void)
{
    int i;
    for (i=0; i<if_pointer; i++) {
        if (!IfArray[i].was_constant) {
            return 0;
        }
    }
    return 1;
}

/***************************************************************/
/*                                                             */
/* pop_excess_ifs - pop excess IFs from the stack, printing    */
/*                  error messages as needed.                  */
/*                  Also resets return_encountered             */
/*                                                             */
/***************************************************************/
void
pop_excess_ifs(char const *fname)
{
    return_encountered = 0;
    if (if_pointer <= base_pointer) {
        return;
    }
    if (!Hush) {
        Eprint("%s", GetErr(E_MISS_ENDIF));
    }
    while(if_pointer > base_pointer) {
        if (!Hush) {
            fprintf(ErrFp, tr("%s(%d): IF without ENDIF"), fname, IfArray[if_pointer-1].lineno);
            fprintf(ErrFp, "\n");
        }
        if_pointer--;
    }
}
