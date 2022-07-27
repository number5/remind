/***************************************************************/
/*                                                             */
/*  OMIT.C                                                     */
/*                                                             */
/*  This file handles all global OMIT commands, and maintains  */
/*  the data structures for OMITted dates.                     */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2022 by Dianne Skoll                    */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>

#include <stdlib.h>
#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"
#include "expr.h"

static int BexistsIntArray (int array[], int num, int key);
static void InsertIntoSortedArray (int *array, int num, int key);

/* Arrays for the global omits */
static int FullOmitArray[MAX_FULL_OMITS];
static int PartialOmitArray[MAX_PARTIAL_OMITS];

/* How many of each omit types do we have? */
static int NumFullOmits, NumPartialOmits;

/* The structure for saving and restoring OMIT contexts */
typedef struct omitcontext {
    struct omitcontext *next;
    int numfull, numpart;
    int *fullsave;
    int *partsave;
} OmitContext;

/* The stack of saved omit contexts */
static OmitContext *SavedOmitContexts = NULL;

/***************************************************************/
/*                                                             */
/*  ClearGlobalOmits                                           */
/*                                                             */
/*  Clear all the global OMIT context.                         */
/*                                                             */
/***************************************************************/
int ClearGlobalOmits(void)
{
    NumFullOmits = NumPartialOmits = 0;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  DoClear                                                    */
/*                                                             */
/*  The command-line function CLEAR-OMIT-CONTEXT               */
/*                                                             */
/***************************************************************/
int DoClear(ParsePtr p)
{
    ClearGlobalOmits();
    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  DestroyOmitContexts                                        */
/*                                                             */
/*  Free all the memory used by saved OMIT contexts.           */
/*  As a side effect, return the number of OMIT contexts       */
/*  destroyed.                                                 */
/*                                                             */
/***************************************************************/
int DestroyOmitContexts(void)
{
    OmitContext *c = SavedOmitContexts;
    OmitContext *d;
    int num = 0;

    while (c) {
	num++;
	if (c->fullsave) free(c->fullsave);
	if (c->partsave) free(c->partsave);
	d = c->next;
	free(c);
	c = d;
    }
    SavedOmitContexts = NULL;
    return num;
}

/***************************************************************/
/*                                                             */
/*  PushOmitContext                                            */
/*                                                             */
/*  Push the OMIT context on to the stack.                     */
/*                                                             */
/***************************************************************/
int PushOmitContext(ParsePtr p)
{
    register int i;
    OmitContext *context;

/* Create the saved context */
    context = NEW(OmitContext);
    if (!context) return E_NO_MEM;

    context->numfull = NumFullOmits;
    context->numpart = NumPartialOmits;
    context->fullsave = malloc(NumFullOmits * sizeof(int));
    if (NumFullOmits && !context->fullsave) {
	free(context);
	return E_NO_MEM;
    }
    context->partsave = malloc(NumPartialOmits * sizeof(int));
    if (NumPartialOmits && !context->partsave) {
	free(context->fullsave);
	free(context);
	return E_NO_MEM;
    }

/* Copy the context over */
    for (i=0; i<NumFullOmits; i++)
	*(context->fullsave + i) = FullOmitArray[i];

    for (i=0; i<NumPartialOmits; i++)
	*(context->partsave + i) = PartialOmitArray[i];

/* Add the context to the stack */
    context->next = SavedOmitContexts;
    SavedOmitContexts = context;
    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  PopOmitContext                                             */
/*                                                             */
/*  Pop the OMIT context off of the stack.                     */
/*                                                             */
/***************************************************************/
int PopOmitContext(ParsePtr p)
{

    register int i;
    OmitContext *c = SavedOmitContexts;

    if (!c) return E_POP_NO_PUSH;
    NumFullOmits = c->numfull;
    NumPartialOmits = c->numpart;

/* Copy the context over */
    for (i=0; i<NumFullOmits; i++)
	FullOmitArray[i] = *(c->fullsave + i);

    for (i=0; i<NumPartialOmits; i++)
	PartialOmitArray[i] = *(c->partsave + i);

/* Remove the context from the stack */
    SavedOmitContexts = c->next;

/* Free memory used by the saved context */
    if (c->partsave) free(c->partsave);
    if (c->fullsave) free(c->fullsave);
    free(c);

    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  IsOmitted                                                  */
/*                                                             */
/*  Set *omit to non-zero if date is omitted, else 0.  Returns */
/*  OK or an error code.                                       */
/*                                                             */
/***************************************************************/
int IsOmitted(int jul, int localomit, char const *omitfunc, int *omit)
{
    int y, m, d;

    /* If we have an omitfunc, we *only* use it and ignore local/global
       OMITs */
    if (omitfunc && *omitfunc && UserFuncExists(omitfunc)) {
	char expr[VAR_NAME_LEN + 32];
	char const *s;
	int r;
	Value v;

	FromJulian(jul, &y, &m, &d);
	sprintf(expr, "%s('%04d-%02d-%02d')",
		omitfunc, y, m+1, d);
	s = expr;
	r = EvalExpr(&s, &v, NULL);
	if (r) return r;
	if (v.type == INT_TYPE && v.v.val != 0) {
	    *omit = 1;
	} else {
	    *omit = 0;
	}
	return OK;
    }

    /* Is it omitted because of local omits? */
    if (localomit & (1 << (jul % 7))) {
	*omit = 1;
	return OK;
    }

    /* Is it omitted because of fully-specified omits? */
    if (BexistsIntArray(FullOmitArray, NumFullOmits, jul)) {
	*omit = 1;
	return OK;
    }

    FromJulian(jul, &y, &m, &d);
    if (BexistsIntArray(PartialOmitArray, NumPartialOmits, (m << 5) + d)) {
	*omit = 1;
	return OK;
    }

    /* Not omitted */
    *omit = 0;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  BexistsIntArray                                            */
/*                                                             */
/*  Perform a binary search on an integer array.  Return 1 if  */
/*  element is found, 0 otherwise.                             */
/*                                                             */
/***************************************************************/
static int BexistsIntArray(int array[], int num, int key)
{
    int top=num-1, bot=0, mid;

    while (top >= bot) {
	mid = (top+bot)/2;
	if (array[mid] == key) return 1;
	else if (array[mid] > key) top = mid-1;
	else bot=mid+1;
    }
    return 0;
}

/***************************************************************/
/*                                                             */
/*  InsertIntoSortedArray                                      */
/*                                                             */
/*  Insert a key into a sorted array.  We assume that there is */
/*  room in the array for it.                                  */
/*                                                             */
/***************************************************************/
static void InsertIntoSortedArray(int *array, int num, int key)
{
    /* num is number of elements CURRENTLY in the array. */
    int *cur = array+num;

    while (cur > array && *(cur-1) > key) {
	*cur = *(cur-1);
	cur--;
    }
    *cur = key;
}

static void DumpOmits(void);

/***************************************************************/
/*                                                             */
/*  DoOmit                                                     */
/*                                                             */
/*  Do a global OMIT command.                                  */
/*                                                             */
/***************************************************************/
int DoOmit(ParsePtr p)
{
    int y[2] = {NO_YR, NO_YR}, m[2] = {NO_MON, NO_MON}, d[2] = {NO_DAY, NO_DAY}, r;
    Token tok;
    int parsing = 1;
    int seen_through = 0;
    int syndrome;
    int not_first_token = -1;
    int start, end, tmp;

    int mc, dc;

    DynamicBuffer buf;
    DBufInit(&buf);

/* Parse the OMIT.  We need a month and day; year is optional. */
    while(parsing) {
	not_first_token++;
	if ( (r=ParseToken(p, &buf)) ) return r;
	FindToken(DBufValue(&buf), &tok);
	switch (tok.type) {
	case T_Dumpvars:
	    if (not_first_token) return E_PARSE_ERR;
	    DBufFree(&buf);
	    r = VerifyEoln(p);
	    if (r != OK) return r;
	    DumpOmits();
	    return OK;

	case T_Date:
	    DBufFree(&buf);
	    if (y[seen_through] != NO_YR) return E_YR_TWICE;
	    if (m[seen_through] != NO_MON) return E_MON_TWICE;
	    if (d[seen_through] != NO_DAY) return E_DAY_TWICE;
	    FromJulian(tok.val, &y[seen_through], &m[seen_through], &d[seen_through]);
	    break;

	case T_Year:
	    DBufFree(&buf);
	    if (y[seen_through] != NO_YR) return E_YR_TWICE;
	    y[seen_through] = tok.val;
	    break;

	case T_Month:
	    DBufFree(&buf);
	    if (m[seen_through] != NO_MON) return E_MON_TWICE;
	    m[seen_through] = tok.val;
	    break;

	case T_Day:
	    DBufFree(&buf);
	    if (d[seen_through] != NO_DAY) return E_DAY_TWICE;
	    d[seen_through] = tok.val;
	    break;

	case T_Delta:
	    DBufFree(&buf);
	    break;

	case T_Through:
	    DBufFree(&buf);
            if (seen_through) return E_UNTIL_TWICE;
            seen_through = 1;
            break;

	case T_Empty:
	case T_Comment:
	case T_RemType:
	case T_Priority:
	case T_Tag:
	case T_Duration:
	    DBufFree(&buf);
	    parsing = 0;
	    break;

	default:
	    Eprint("%s: `%s' (OMIT)", ErrMsg[E_UNKNOWN_TOKEN],
		   DBufValue(&buf));
	    DBufFree(&buf);
	    return E_UNKNOWN_TOKEN;
	}
    }

    if (!seen_through) {
        /* We must have at least a month */
        if (m[0] == NO_MON) return E_SPEC_MON_DAY;
        m[1] = m[0];
        y[1] = y[0];
        if (d[0] == NO_DAY) {
            d[0] = 1;
            if (y[0] == NO_YR) {
                d[1] = MonthDays[m[0]];
            } else {
                d[1] = DaysInMonth(m[0], y[0]);
            }
        } else {
            d[1] = d[0];
            m[1] = m[0];
            y[1] = y[0];
        }
    } else {
        if (m[0] == NO_MON) return E_SPEC_MON_DAY;
        if (m[1] == NO_MON) return E_SPEC_MON_DAY;
        if ((y[0] != NO_YR && y[1] == NO_YR) ||
            (y[0] == NO_YR && y[1] != NO_YR)) {
            return E_BAD_DATE;
        }
        if (d[0] == NO_DAY) d[0] = 1;
        if (d[1] == NO_DAY) {
            if (y[1] == NO_YR) {
                d[1] = MonthDays[m[1]];
            } else {
                d[1] = DaysInMonth(m[1], y[1]);
            }
        }
    }

    if (y[0] == NO_YR) {
        /* Partial OMITs */
	if (d[0] > MonthDays[m[0]]) return E_BAD_DATE;
	if (d[1] > MonthDays[m[1]]) return E_BAD_DATE;
        dc = d[0];
        mc = m[0];
        while(1) {
            syndrome = (mc<<5) + dc;
            if (!BexistsIntArray(PartialOmitArray, NumPartialOmits, syndrome)) {
                InsertIntoSortedArray(PartialOmitArray, NumPartialOmits, syndrome);
                NumPartialOmits++;
            }
            if (mc == m[1] && dc == d[1]) {
                break;
            }
            dc++;
            if (dc > MonthDays[mc]) {
                dc = 1;
                mc++;
                if (mc > 11) {
                    mc = 0;
                }
            }
        }
    } else {
        /* Full OMITs */
	if (d[0] > DaysInMonth(m[0], y[0])) return E_BAD_DATE;
	if (d[1] > DaysInMonth(m[1], y[1])) return E_BAD_DATE;
        start = Julian(y[0], m[0], d[0]);
        end   = Julian(y[1], m[1], d[1]);
        if (end < start) {
            Wprint("Warning: Swapping dates on OMIT ... THROUGH ... line");
            tmp = start;
            start = end;
            end = tmp;
        }
        for (tmp = start; tmp <= end; tmp++) {
            if (!BexistsIntArray(FullOmitArray, NumFullOmits, tmp)) {
                if (NumFullOmits >= MAX_FULL_OMITS) return E_2MANY_FULL;
                InsertIntoSortedArray(FullOmitArray, NumFullOmits, tmp);
                NumFullOmits++;
            }
        }
    }

    if (tok.type == T_Tag || tok.type == T_Duration || tok.type == T_RemType || tok.type == T_Priority) return E_PARSE_AS_REM;
    return OK;

}

int
AddGlobalOmit(int jul)
{
    if (NumFullOmits == MAX_FULL_OMITS) return E_2MANY_FULL;
    if (!BexistsIntArray(FullOmitArray, NumFullOmits, jul)) {
        InsertIntoSortedArray(FullOmitArray, NumFullOmits, jul);
        NumFullOmits++;
    }
    return OK;
}

void
DumpOmits(void)
{
    int i;
    int y, m, d;
    printf("Global Full OMITs (%d of maximum allowed %d):\n", NumFullOmits, MAX_FULL_OMITS);
    if (!NumFullOmits) {
	printf("\tNone.\n");
    } else {
	for (i=0; i<NumFullOmits; i++) {
	    FromJulian(FullOmitArray[i], &y, &m, &d);
	    printf("\t%04d%c%02d%c%02d\n",
		    y, DateSep, m+1, DateSep, d);
	}
    }
    printf("Global Partial OMITs (%d of maximum allowed %d):\n", NumPartialOmits, MAX_PARTIAL_OMITS);
    if (!NumPartialOmits) {
	printf("\tNone.\n");
    } else {
	for (i=0; i<NumPartialOmits; i++) {
	    m = PartialOmitArray[i] >> 5 & 0xf;
	    d = PartialOmitArray[i] & 0x1f;
	    printf("\t%02d%c%02d\n", m+1, DateSep, d);
	}
    }
}

