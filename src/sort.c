/***************************************************************/
/*                                                             */
/*  SORT.C                                                     */
/*                                                             */
/*  Routines for sorting reminders by trigger date             */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2026 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"

/* The structure of a sorted entry */
typedef struct sortrem {
    struct sortrem *next;
    char const *text;
    char const *url;
    int trigdate;
    int trigtime;
    int typ;
    int priority;
} Sortrem;

/* The sorted reminder queue */
static Sortrem *SortedQueue = (Sortrem *) NULL;

static Sortrem *MakeSortRem (int dse, int tim, char const *url, char const *body, int typ, int prio);
static void IssueSortBanner (int dse);

/***************************************************************/
/*                                                             */
/*  MakeSortRem                                                */
/*                                                             */
/*  Create a new Sortrem entry - return NULL on failure.       */
/*                                                             */
/***************************************************************/
static Sortrem *MakeSortRem(int dse, int tim, char const *url, char const *body, int typ, int prio)
{
    Sortrem *srem = NEW(Sortrem);
    if (!srem) return NULL;

    srem->text = strdup(body);
    if (!srem->text) {
        free(srem);
        return NULL;
    }
    if (url) {
        srem->url = strdup(url);
        if (!srem->url) {
            free((char *) srem->text);
            free(srem);
            return NULL;
        }
    } else {
        srem->url = NULL;
    }

    srem->trigdate = dse;
    srem->trigtime = tim;
    srem->typ = typ;
    srem->priority = prio;
    srem->next = NULL;
    return srem;
}

/***************************************************************/
/*                                                             */
/*  InsertIntoSortBuffer                                       */
/*                                                             */
/*  Insert a reminder into the sort buffer                     */
/*                                                             */
/***************************************************************/
int InsertIntoSortBuffer(int dse, int tim, char const *url, char const *body, int typ, int prio)
{
    Sortrem *srem = MakeSortRem(dse, tim, url, body, typ, prio);
    Sortrem *cur = SortedQueue, *prev = NULL;
    int ShouldGoAfter;

    if (!srem) {
        Eprint("%s", GetErr(E_NO_MEM));
        IssueSortedReminders();
        SortByDate = 0;
        SortByTime = 0;
        SortByPrio = 0;
        UntimedBeforeTimed = 0;
        return E_NO_MEM;
    }

    /* Find the correct place in the sorted list */
    if (!SortedQueue) {
        SortedQueue = srem;
        return OK;
    }
    while (cur) {
        ShouldGoAfter = CompareRems(srem->trigdate, srem->trigtime, srem->priority,
                                    cur->trigdate, cur->trigtime, cur->priority,
                                    SortByDate, SortByTime, SortByPrio, UntimedBeforeTimed);

        if (ShouldGoAfter <= 0) {
            prev = cur;
            cur = cur->next;
        } else {
            if (prev) {
                prev->next = srem;
                srem->next = cur;
            } else {
                SortedQueue = srem;
                srem->next = cur;
            }
            return OK;
        }

    }
    prev->next = srem;
    srem->next = cur;  /* For safety - actually redundant */
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
                DoMsgCommand(MsgCommand, cur->text, 0);
            } else {
                if (cur->trigdate != olddate) {
                    IssueSortBanner(cur->trigdate);
                    olddate = cur->trigdate;
                }
                if (cur->url) {
                    printf("\x1B]8;;%s\x1B\\", cur->url);
                }
                printf("%s", cur->text);
                if (cur->url) {
                    printf("\x1B]8;;\x1B\\");
                }
            }
            break;

        case MSF_TYPE:
            if (cur->trigdate != olddate) {
                IssueSortBanner(cur->trigdate);
                olddate = cur->trigdate;
            }
            FillParagraph(cur->url, cur->text, NULL);
            break;

        case RUN_TYPE:
            System(cur->text, 0);
            break;
        }

        free((char *) cur->text);
        if (cur->url) {
            free((char *) cur->url);
        }
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
    snprintf(BanExpr, sizeof(BanExpr), "sortbanner('%04d/%02d/%02d')", y, m+1, d);   
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
