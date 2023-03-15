/***************************************************************/
/*                                                             */
/*  SORT.C                                                     */
/*                                                             */
/*  Routines for sorting reminders by trigger date             */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2023 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include "types.h"
#include "protos.h"
#include "expr.h"
#include "globals.h"
#include "err.h"

/* The structure of a sorted entry */
typedef struct sortrem {
    struct sortrem *next;
    char const *text;
    int trigdate;
    int trigtime;
    int typ;
    int priority;
} Sortrem;

/* The sorted reminder queue */
static Sortrem *SortedQueue = (Sortrem *) NULL;

static Sortrem *MakeSortRem (int dse, int tim, char const *body, int typ, int prio);
static void IssueSortBanner (int dse);

/***************************************************************/
/*                                                             */
/*  MakeSortRem                                                */
/*                                                             */
/*  Create a new Sortrem entry - return NULL on failure.       */
/*                                                             */
/***************************************************************/
static Sortrem *MakeSortRem(int dse, int tim, char const *body, int typ, int prio)
{
    Sortrem *new = NEW(Sortrem);
    if (!new) return NULL;

    new->text = StrDup(body);
    if (!new->text) {
	free(new);
	return NULL;
    }
  
    new->trigdate = dse;
    new->trigtime = tim;
    new->typ = typ;
    new->priority = prio;
    new->next = NULL;
    return new;
}

/***************************************************************/
/*                                                             */
/*  InsertIntoSortBuffer                                       */
/*                                                             */
/*  Insert a reminder into the sort buffer                     */
/*                                                             */
/***************************************************************/
int InsertIntoSortBuffer(int dse, int tim, char const *body, int typ, int prio)
{
    Sortrem *new = MakeSortRem(dse, tim, body, typ, prio);
    Sortrem *cur = SortedQueue, *prev = NULL;
    int ShouldGoAfter;

    if (!new) {
	Eprint("%s", ErrMsg[E_NO_MEM]);
	IssueSortedReminders();
	SortByDate = 0;
	SortByTime = 0;
	SortByPrio = 0;
	UntimedBeforeTimed = 0;
	return E_NO_MEM;
    }

    /* Find the correct place in the sorted list */
    if (!SortedQueue) {
	SortedQueue = new;
	return OK;
    }
    while (cur) {
	ShouldGoAfter = CompareRems(new->trigdate, new->trigtime, new->priority,
				    cur->trigdate, cur->trigtime, cur->priority,
				    SortByDate, SortByTime, SortByPrio, UntimedBeforeTimed);

	if (ShouldGoAfter <= 0) {
	    prev = cur;
	    cur = cur->next;
	} else {
	    if (prev) {
		prev->next = new;
		new->next = cur;
	    } else {
		SortedQueue = new;
		new->next = cur;
	    }
	    return OK;
	}
      
    }
    prev->next = new;
    new->next = cur;  /* For safety - actually redundant */
    return OK;
}

   
/***************************************************************/
/*                                                             */
/*  IssueSortedReminders                                       */
/*                                                             */
/*  Issue all of the sorted reminders and free memory.         */
/*                                                             */
/***************************************************************/
void IssueSortedReminders(void)
{
    Sortrem *cur = SortedQueue;
    Sortrem *next;
    int olddate = NO_DATE;

    while (cur) {
	next = cur->next;
	switch(cur->typ) {
	case MSG_TYPE:
	    if (MsgCommand && *MsgCommand) {
		DoMsgCommand(MsgCommand, cur->text);
            } else {
		if (cur->trigdate != olddate) {
		    IssueSortBanner(cur->trigdate);
		    olddate = cur->trigdate;
		}
		printf("%s", cur->text);
            }
	    break;

	case MSF_TYPE:
            if (cur->trigdate != olddate) {
                IssueSortBanner(cur->trigdate);
                olddate = cur->trigdate;
            }
	    FillParagraph(cur->text);
	    break;

	case RUN_TYPE:
	    System(cur->text);
	    break;
	}

	free((char *) cur->text);
	free(cur);
	cur = next;
    }
    SortedQueue = NULL;
}
/***************************************************************/
/*                                                             */
/*  IssueSortBanner                                            */
/*                                                             */
/*  Issue a daily banner if the function sortbanner() is       */
/*  defined to take one argument.                              */
/*                                                             */
/***************************************************************/
static void IssueSortBanner(int dse)
{
    char BanExpr[64];
    int y, m, d;
    Value v;
    char const *s = BanExpr;
    DynamicBuffer buf;

    if (UserFuncExists("sortbanner") != 1) return;

    FromDSE(dse, &y, &m, &d);
    sprintf(BanExpr, "sortbanner('%04d/%02d/%02d')", y, m+1, d);   
    y = EvalExpr(&s, &v, NULL);
    if (y) return;
    if (DoCoerce(STR_TYPE, &v)) return;
    DBufInit(&buf);
    if (!DoSubstFromString(v.v.str, &buf, dse, NO_TIME)) {
	if (*DBufValue(&buf)) printf("%s\n", DBufValue(&buf));
	DBufFree(&buf);
    }
    DestroyValue(v);
}

/***************************************************************/
/*                                                             */
/*  CompareRems                                                */
/*                                                             */
/*  Compare two reminders for sorting.  Return 0 if they       */
/*  compare equal; 1 if rem2 should come after rem1, -1 if     */
/*  rem1 should come after rem2.  bydate and bytime control    */
/*  sorting direction by date and time, resp.                  */
/*                                                             */
/***************************************************************/
int CompareRems(int dat1, int tim1, int prio1,
		int dat2, int tim2, int prio2,
		int bydate, int bytime, int byprio,
		int untimed_first)
{
    int dafter, tafter, pafter, uafter;

    dafter = (bydate != SORT_DESCEND) ? 1 : -1;
    tafter = (bytime != SORT_DESCEND) ? 1 : -1;
    pafter = (byprio != SORT_DESCEND) ? 1 : -1;
    uafter = (untimed_first) ? -1 : 1;

    if (dat1 < dat2) return dafter;
    if (dat1 > dat2) return -dafter;

    if (tim1 == NO_TIME && tim2 != NO_TIME) {
	return -uafter;
    }

    if (tim1 != NO_TIME && tim2 == NO_TIME) {
	return uafter;
    }

    if (tim1 < tim2) return tafter;
    if (tim1 > tim2) return -tafter;

    if (prio1 < prio2) return pafter;
    if (prio1 > prio2) return -pafter;

    return 0;
}
