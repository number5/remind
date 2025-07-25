/***************************************************************/
/*                                                             */
/*  TRIGGER.C                                                  */
/*                                                             */
/*  Routines for figuring out the trigger date of a reminder   */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"

#define GOT_DAY 1
#define GOT_MON 2
#define GOT_YR 4
#define GOT_WD 8

#define ADVANCE_TO_WD(x, wd) while (! ((wd) & (1 << ((x)%7)))) (x)++

static int DSEYear(int dse);
static int DSEMonth(int dse);
static int NextSimpleTrig(int startdate, Trigger const *trig, int *err);
static int GetNextTriggerDate(Trigger *trig, int start, int *err, int *nextstart);
static int TrigInfoIsValid(char const *info);
static int TrigInfoHeadersAreTheSame(char const *i1, char const *i2);

/***************************************************************/
/*                                                             */
/*  NextSimpleTrig                                             */
/*                                                             */
/*  Compute the "simple" trigger date, taking into account     */
/*  ONLY the day of week, day, month and year components.      */
/*  Normally, returns -1 if the trigger has expired.  As a     */
/*  special case, if D, M, Y [WD] are specified, returns the   */
/*  DSE date, regardless of whether it's expired.  This is     */
/*  so that dates with a REP can be handled properly.          */
/*                                                             */
/***************************************************************/
static int NextSimpleTrig(int startdate, Trigger const *trig, int *err)
{
    int typ = 0;
    int d, m, y, j, d2, m2, y2;

    *err = 0;
    FromDSE(startdate, &y, &m, &d);
    d2 = d;
    m2 = m;
    y2 = y;

    if (trig->d != NO_DAY) typ |= GOT_DAY;
    if (trig->m != NO_MON) typ |= GOT_MON;
    if (trig->y != NO_YR) typ |= GOT_YR;
    if (trig->wd != NO_WD) typ |= GOT_WD;
    switch(typ) {
    case 0:
        return startdate;

    case GOT_WD:
        ADVANCE_TO_WD(startdate, trig->wd);
        return startdate;

    case GOT_DAY:
        if (d > trig->d) {
            m++;
            if (m == 12) { m = 0; y++; }
        }
        while (trig->d > DaysInMonth(m, y)) {
            m++;
            if (m == 12) { m = 0; y++; }
        }
        j = DSE(y, m, trig->d);
        return j;

    case GOT_MON:
        if (m == trig->m) return startdate;
        else if (m > trig->m) return DSE(y+1, trig->m, 1);
        else return DSE(y, trig->m, 1);

    case GOT_YR:
        if (y == trig->y) return startdate;
        else if (y < trig->y) return DSE(trig->y, 0, 1);
        else return -1;

    case GOT_DAY+GOT_MON:
        if (trig->d > MonthDays[trig->m]) {
            *err = E_BAD_DATE;
            return -1;
        }

        if (m > trig->m || (m == trig->m && d > trig->d)) y++;
        /* Take care of Feb. 29 */
        while (trig->d > DaysInMonth(trig->m, y)) y++;
        return DSE(y, trig->m, trig->d);

    case GOT_DAY+GOT_YR:
        if (y < trig->y) return DSE(trig->y, 0, trig->d);
        else if (y > trig->y) return -1;

        if (d > trig->d) {
            m++;
            if (m == 12) return -1;
        }
        while (trig->d > DaysInMonth(m, trig->y)) m++;
        return DSE(trig->y, m, trig->d);

    case GOT_MON+GOT_YR:
        if (y > trig->y || (y == trig->y && m > trig->m)) return -1;
        if (y < trig->y) return DSE(trig->y, trig->m, 1);
        if (m == trig->m) return startdate;
        return DSE(trig->y, trig->m, 1);

    case GOT_DAY+GOT_MON+GOT_YR:
        if (trig->d > DaysInMonth(trig->m, trig->y)) {
            *err = E_BAD_DATE;
            return -1;
        }
        return DSE(trig->y, trig->m, trig->d);

    case GOT_YR+GOT_WD:
        if (y > trig->y) return -1;
        if (y < trig->y) j = DSE(trig->y, 0, 1);
        else j = startdate;
        ADVANCE_TO_WD(j, trig->wd);
        if (DSEYear(j) > trig->y) return -1;
        return j;

    case GOT_MON+GOT_WD:
        if (m == trig->m) {
            j = startdate;
            ADVANCE_TO_WD(j, trig->wd);
            if (DSEMonth(j) == trig->m) return j;
        }
        if (m >= trig->m) j = DSE(y+1, trig->m, 1);
        else j = DSE(y, trig->m, 1);
        ADVANCE_TO_WD(j, trig->wd);
        return j; /* Guaranteed to be within the month */

    case GOT_DAY+GOT_WD:
        if (m !=0 || y > BASE) {
            m2 = m-1;
            if (m2 < 0) { y2 = y-1; m2 = 11; }

            /* If there are fewer days in previous month, no match */
            if (trig->d <= DaysInMonth(m2, y2)) {
                j = DSE(y2, m2, trig->d);
                ADVANCE_TO_WD(j, trig->wd);
                if (j >= startdate) return j;

            }
        }

        /* Try this month */
        if (trig->d <= DaysInMonth(m, y)) {
            j = DSE(y, m, trig->d);
            ADVANCE_TO_WD(j, trig->wd);
            if (j >= startdate) return j;
        }

        /* Argh!  Try next avail. month */
        m2 = m+1;
        if (m2 > 11) { m2 = 0; y++; }
        while (trig->d > DaysInMonth(m2, y)) m2++;
        j = DSE(y, m2, trig->d);
        ADVANCE_TO_WD(j, trig->wd);
        return j;

    case GOT_WD+GOT_YR+GOT_DAY:
        if (y > trig->y+1 || (y > trig->y && m>0)) return -1;
        if (y > trig->y) {
            j = DSE(trig->y, 11, trig->d);
            ADVANCE_TO_WD(j, trig->wd);
            if (j >= startdate) return j;
        } else if (y < trig->y) {
            j = DSE(trig->y, 0, trig->d);
            ADVANCE_TO_WD(j, trig->wd);
            return j;
        } else {
            /* Try last month */
            if (m > 0) {
                m2 = m-1;
                while (trig->d > DaysInMonth(m2, trig->y)) m2--;
                j = DSE(trig->y, m2, trig->d);
                ADVANCE_TO_WD(j, trig->wd);
                if (DSEYear(j) > trig->y) return -1;
                if (j >= startdate) return j;
            }
        }
        /* Try this month */
        if (trig->d <= DaysInMonth(m, trig->y)) {
            j = DSE(trig->y, m, trig->d);
            ADVANCE_TO_WD(j, trig->wd);
            if (DSEYear(j) > trig->y) return -1;
            if (j >= startdate) return j;
        }

        /* Must be next month */
        if (m == 11) return -1;
        m++;
        while (trig->d > DaysInMonth(m, trig->d)) m++;
        j = DSE(trig->y, m, trig->d);
        ADVANCE_TO_WD(j, trig->wd);
        if (DSEYear(j) > trig->y) return -1;
        return j;

    case GOT_DAY+GOT_MON+GOT_WD:
        if (trig->d > MonthDays[trig->m]) {
            *err = E_BAD_DATE;
            return -1;
        }
        /* Back up a year in case we'll cross a year boundary*/
        if (y > BASE) {
            y--;
        }

        /* Move up to the first valid year */
        while (trig->d > DaysInMonth(trig->m, y)) y++;

        /* Try last year */
        j = DSE(y, trig->m, trig->d);
        ADVANCE_TO_WD(j, trig->wd);
        if (j >= startdate) return j;

        /* Try this year */
        y++;
        while (trig->d > DaysInMonth(trig->m, y)) y++;
        j = DSE(y, trig->m, trig->d);
        ADVANCE_TO_WD(j, trig->wd);
        if (j >= startdate) return j;

        /* Must be next year */
        y++;
        while (trig->d > DaysInMonth(trig->m, y)) y++;
        j = DSE(y, trig->m, trig->d);
        ADVANCE_TO_WD(j, trig->wd);
        return j;

    case GOT_WD+GOT_MON+GOT_YR:
        if (y > trig->y || (y == trig->y && m > trig->m)) return -1;
        /* cppcheck-suppress knownConditionTrueFalse */
        if (trig->y > y || (trig->y == y && trig->m > m)) {
            j = DSE(trig->y, trig->m, 1);
            ADVANCE_TO_WD(j, trig->wd);
            return j;
        } else {
            j = startdate;
            ADVANCE_TO_WD(j, trig->wd);
            FromDSE(j, &y2, &m2, &d2);
            if (m2 == trig->m) return j; else return -1;
        }

    case GOT_WD+GOT_DAY+GOT_MON+GOT_YR:
        if (trig->d > DaysInMonth(trig->m, trig->y)) {
            *err = E_BAD_DATE;
            return -1;
        }
        j = DSE(trig->y, trig->m, trig->d);
        ADVANCE_TO_WD(j, trig->wd);
        return j;

    default:
        Eprint("NextSimpleTrig %s %d", GetErr(E_SWERR), typ);
        *err = E_SWERR;
        return -1;
    }
}

/***************************************************************/
/*                                                             */
/*  DSEMonth - Given a DSE date, what's the month?             */
/*                                                             */
/***************************************************************/
static int DSEMonth(int dse)
{
    int y, m, d;
    FromDSE(dse, &y, &m, &d);
    return m;
}

/***************************************************************/
/*                                                             */
/*  DSEYear - Given a DSE date, what's the year?               */
/*                                                             */
/***************************************************************/
static int DSEYear(int dse)
{
    int y, m, d;
    FromDSE(dse, &y, &m, &d);
    return y;
}

/***************************************************************/
/*                                                             */
/*  GetNextTriggerDate                                         */
/*                                                             */
/*  Given a trigger, compute the next trigger date.            */
/*                                                             */
/*  Returns the DSE date of next trigger, -1 if                */
/*  expired, -2 if can't compute trigger date.                 */
/*                                                             */
/***************************************************************/
static int GetNextTriggerDate(Trigger *trig, int start, int *err, int *nextstart)
{
    int simple, mod, omit;

    /* First:  Have we passed the UNTIL date? */
    if (trig->until != NO_UNTIL &&
        trig->until < start) {
        trig->expired = 1;
        return -1; /* expired */
    }

    /* Next: If it's an "AFTER"-type skip, back up
       until we're at the start of a block of holidays */
    if (trig->skip == AFTER_SKIP) {
        int iter = 0;
        while (iter++ <= MaxSatIter) {
            *err = IsOmitted(start-1, trig->localomit, trig->omitfunc, &omit);
            if (*err) return -2;
            if (!omit) {
                break;
            }
            start--;
            if (start < 0) {
                break;
            }
        }
        if (start < 0 || iter > MaxSatIter) {
            /* omitfunc must have returned "true" too often */
            *err = E_CANT_TRIG;
            return -2;
        }
    }

    /* Find the next simple trigger */
    simple = NextSimpleTrig(start, trig, err);

    /* Problems? */
    if (*err || (simple == -1)) return -1;

    /* Suggested starting point for next attempt */
    *nextstart = simple+1;

    /* If there's a BACK, back up... */
    if (trig->back != NO_BACK) {
        mod = trig->back;
        if (mod < 0) {
            simple += mod;
        }
        else {
            int iter = 0;
            int max = MaxSatIter;
            if (max < mod*2) {
                max = mod*2;
            }
            while(iter++ <= max) {
                if (!mod) {
                    break;
                }
                simple--;
                *err = IsOmitted(simple, trig->localomit, trig->omitfunc, &omit);
                if (*err) return -2;
                if (!omit) mod--;
            }
            if (iter > max) {
                *err = E_CANT_TRIG;
                return -2;
            }
        }
    }

    /* If there's a REP, calculate the next occurrence */
    if (trig->rep != NO_REP) {
        if (simple < start) {
            mod = (start - simple) / trig->rep;
            simple = simple + mod * trig->rep;
            if (simple < start) simple += trig->rep;
        }
    }

    /* If it's a "BEFORE"-type skip, back up */
    if (trig->skip == BEFORE_SKIP) {
        int iter = 0;
        while(iter++ <= MaxSatIter) {
            *err = IsOmitted(simple, trig->localomit, trig->omitfunc, &omit);
            if (*err) return -2;
            if (!omit) {
                break;
            }
            simple--;
            if (simple < 0) {
                *err = E_CANT_TRIG;
                return -2;
            }
        }
        if (iter > MaxSatIter) {
            *err = E_CANT_TRIG;
            return -2;
        }
    }

    /* If it's an "AFTER"-type skip, jump ahead */
    if (trig->skip == AFTER_SKIP) {
        int iter = 0;
        while (iter++ <= MaxSatIter) {
            *err = IsOmitted(simple, trig->localomit, trig->omitfunc, &omit);
            if (*err) return -2;
            if (!omit) {
                break;
            }
            simple++;
        }
        if (iter > MaxSatIter) {
            *err = E_CANT_TRIG;
            return -2;
        }
    }

    /* If we've passed the UNTIL, then it's expired */
    if (trig->until != NO_UNTIL && simple > trig->until) {
        return -1;
    }

    /* Return the date */
    return simple;
}

int
AdjustTriggerForDuration(int today, int r, Trigger *trig, TimeTrig *tim, int save_in_globals)
{
    int y, m, d;
    /* If we have an AT, save the original event start */
    if (tim->ttime != NO_TIME) {
        trig->eventstart = MINUTES_PER_DAY * r + tim->ttime;
        if (tim->duration != NO_TIME) {
            trig->eventduration = tim->duration;
        }
    }

    /* Now potentially adjust */
    if (r < today && r + trig->duration_days >= today) {
        /* Adjust duration down */
        tim->duration -= (today - r) * MINUTES_PER_DAY;
        tim->duration += tim->ttime;

        /* Start at midnight */
        tim->ttime = 0;

        /* Change trigger date to today */
        r = today;
        if (DebugFlag & DB_PRTTRIG) {
            FromDSE(r, &y, &m, &d);
            fprintf(ErrFp, "%s(%s): Trig(adj) = %s, %d %s, %d",
                    GetCurrentFilename(), line_range(LineNoStart, LineNo),
                    get_day_name(r % 7),
                    d,
                    get_month_name(m),
                    y);
            if (tim->ttime != NO_TIME) {
                fprintf(ErrFp, " AT %02d:%02d",
                        (tim->ttime / 60),
                        (tim->ttime % 60));
                if (tim->duration != NO_TIME) {
                    fprintf(ErrFp, " DURATION %02d:%02d",
                            (tim->duration / 60),
                            (tim->duration % 60));
                }
            }
            fprintf(ErrFp, "\n");
        }

    }
    if (save_in_globals) {
        SaveAllTriggerInfo(trig, tim, r, tim->ttime, 1);
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  ComputeTrigger                                             */
/*                                                             */
/*  The main function.  Compute the next trigger date given    */
/*  today's date.                                              */
/*                                                             */
/***************************************************************/
int ComputeTrigger(int today, Trigger *trig, TimeTrig *tim,
                   int *err, int save_in_globals)
{
    int r = ComputeTriggerNoAdjustDuration(today, trig, tim, err, save_in_globals, 0);
    if (*err != OK) {
        return r;
    }
    if (r == today) {
        if (tim->ttime != NO_TIME) {
            trig->eventstart = MINUTES_PER_DAY * r + tim->ttime;
            if (tim->duration != NO_TIME) {
                trig->eventduration = tim->duration;
            }
        }
        if (save_in_globals) {
            SaveAllTriggerInfo(trig, tim, r, tim->ttime, 1);
        }
        return r;
    }

    if (trig->duration_days) {
        r = ComputeTriggerNoAdjustDuration(today, trig, tim, err, save_in_globals, trig->duration_days);
        if (*err != OK) {
            return r;
        }
    }
    r = AdjustTriggerForDuration(today, r, trig, tim, save_in_globals);
    return r;
}

/***************************************************************/
/*                                                             */
/*  ComputeTriggerNoAdjustDuration                             */
/*                                                             */
/*  Compute a trigger, but do NOT adjust the time trigger      */
/*  duration.                                                  */
/*                                                             */
/***************************************************************/
int ComputeTriggerNoAdjustDuration(int today, Trigger *trig, TimeTrig const *tim,
                                   int *err, int save_in_globals, int duration_days)
{
    int nattempts = 0,
        start = today - duration_days,
        nextstart = 0,
        y, m, d, omit,
        result;

    trig->expired = 0;
    if (save_in_globals) {
        LastTrigValid = 0;
        LastTriggerDate = -1;
    }

    /* Assume everything works */
    *err = OK;

    /* But check for obvious problems... */
    if ((WeekdayOmits | trig->localomit) == 0x7F) {
        *err = E_2MANY_LOCALOMIT;
        return -1;
    }

    if (start < 0) {
        *err = E_DATE_OVER;
        return -1;
    }

    if (tim->duration != NO_TIME && tim->ttime == NO_TIME) {
        *err = E_DURATION_NO_AT;
        return -1;
    }

    if (trig->rep != NO_REP &&
        (trig->d == NO_DAY ||
         trig->m == NO_MON ||
         trig->y == NO_YR)) {
        Eprint("%s", GetErr(E_REP_FULSPEC));
        *err = E_REP_FULSPEC;
        return -1;
    }


    /* Save the trigger */
    if (save_in_globals) {
        SaveLastTrigger(trig);
    }

    while (nattempts++ < TRIG_ATTEMPTS) {
        result = GetNextTriggerDate(trig, start, err, &nextstart);
        /* If there's an error, die immediately */
        if (*err) return -1;
        if (result == -1) {
            trig->expired = 1;
            if (DebugFlag & DB_PRTTRIG) {
                fprintf(ErrFp, "%s(%s): %s\n",
                        GetCurrentFilename(), line_range(LineNoStart, LineNo), GetErr(E_EXPIRED));
            }
            return -1;
        }

        /* If result is >= today, great! */
        if (trig->skip == SKIP_SKIP) {
            *err = IsOmitted(result, trig->localomit, trig->omitfunc, &omit);
            if (*err) return -1;
        } else {
            omit = 0;
        }

        /** FIXME: Fix bad interaction with SATISFY... need to rethink!!! */
        if (result+duration_days >= today &&
            (trig->skip != SKIP_SKIP || !omit)) {
            if (save_in_globals) {
                LastTriggerDate = result;  /* Save in global var */
                LastTrigValid = 1;
            }
            if (DebugFlag & DB_PRTTRIG) {
                FromDSE(result, &y, &m, &d);
                fprintf(ErrFp, "%s(%s): Trig = %s, %d %s, %d",
                        GetCurrentFilename(), line_range(LineNoStart, LineNo),
                        get_day_name(result % 7),
                        d,
                        get_month_name(m),
                        y);
                if (tim->ttime != NO_TIME) {
                    fprintf(ErrFp, " AT %02d:%02d",
                            (tim->ttime / 60),
                            (tim->ttime % 60));
                    if (tim->duration != NO_TIME) {
                        fprintf(ErrFp, " DURATION %02d:%02d",
                            (tim->duration / 60),
                            (tim->duration % 60));
                    }
                }
                fprintf(ErrFp, "\n");
            }
            return result;
        }

        /* If it's a simple trigger, no point in rescanning */
        if (trig->back == NO_BACK &&
            trig->skip == NO_SKIP &&
            trig->rep == NO_REP) {
            trig->expired = 1;
            if (DebugFlag & DB_PRTTRIG) {
                FromDSE(result, &y, &m, &d);
                fprintf(ErrFp, "%s(%s): %s: %04d-%02d-%02d\n",
                        GetCurrentFilename(), line_range(LineNoStart, LineNo), GetErr(E_EXPIRED), y, m+1, d);
            }
            if (save_in_globals) {
                LastTriggerDate = result;
                LastTrigValid = 1;
            }
            return -1;
        }

        if (trig->skip == SKIP_SKIP &&
            omit &&
            nextstart <= start &&
            result >= start) {
            nextstart = result + 1;
        }

        /* Keep scanning... unless there's no point in doing it.*/
        if (nextstart <= start) {
            if (save_in_globals) {
                LastTriggerDate = result;
                LastTrigValid = 1;
            }
            trig->expired = 1;
            if (DebugFlag & DB_PRTTRIG) {
                fprintf(ErrFp, "%s(%s): %s\n",
                        GetCurrentFilename(), line_range(LineNoStart, LineNo), GetErr(E_EXPIRED));
            }
            return -1;
        }
        else start = nextstart;

    }

    /* We failed - too many attempts or trigger has expired*/
    *err = E_CANT_TRIG;
    return -1;
}

/***************************************************************/
/*                                                             */
/*  NewTrigInfo                                                */
/*                                                             */
/*  Create a new TrigInfo object with the specified contents.  */
/*  Returns NULL if memory allocation fails.                   */
/*                                                             */
/***************************************************************/
static TrigInfo *
NewTrigInfo(char const *i)
{
    TrigInfo *ti = malloc(sizeof(TrigInfo));

    if (!ti) {
        return NULL;
    }
    ti->next = NULL;
    ti->info = StrDup(i);
    if (!ti->info) {
        free(ti);
        return NULL;
    }
    return ti;
}

/***************************************************************/
/*                                                             */
/*  FreeTrigInfo                                               */
/*                                                             */
/*  Free a TrigInfo objects.                                   */
/*                                                             */
/***************************************************************/
static void
FreeTrigInfo(TrigInfo *ti)
{
    if (ti->info) {
        free( (void *) ti->info);
        ti->info = NULL;
    }
    ti->next = NULL;
    free(ti);
}

void
FreeTrigInfoChain(TrigInfo *ti)
{
    TrigInfo *next;

    while(ti) {
        next = ti->next;
        FreeTrigInfo(ti);
        ti = next;
    }
}

/***************************************************************/
/*                                                             */
/*  AppendTrigInfo                                             */
/*                                                             */
/*  Append an info item to a trigger.                          */
/*                                                             */
/***************************************************************/
int
AppendTrigInfo(Trigger *t, char const *info)
{
    TrigInfo *ti;
    TrigInfo *last;

    if (!TrigInfoIsValid(info)) {
        Eprint("%s", tr("Invalid INFO string: Must be of the form \"Header: Value\""));
        return E_PARSE_ERR;
    }

    ti = NewTrigInfo(info);
    last = t->infos;
    if (!ti) {
        return E_NO_MEM;
    }
    if (!last) {
        t->infos = ti;
        return OK;
    }
    if (TrigInfoHeadersAreTheSame(info, last->info)) {
        Eprint("%s", tr("Duplicate INFO headers are not permitted"));
        FreeTrigInfo(ti);
        return E_PARSE_ERR;
    }
    while (last->next) {
        last = last->next;
        if (TrigInfoHeadersAreTheSame(info, last->info)) {
            Eprint("%s", tr("Duplicate INFO headers are not permitted"));
            FreeTrigInfo(ti);
            return E_PARSE_ERR;
        }
    }
    last->next = ti;
    return OK;
}

static int
TrigInfoHeadersAreTheSame(char const *i1, char const *i2)
{
    char const *c1 = strchr(i1, ':');
    char const *c2 = strchr(i2, ':');
    if (!c1 || !c2) return 1;
    if (c1 - i1 != c2 - i2) return 0;
    if (!strncasecmp(i1, i2, (c1 - i1))) return 1;
    return 0;
}

static int
TrigInfoIsValid(char const *info)
{
    char const *t;
    char const *s = strchr(info, ':');
    if (!s) return 0;
    if (s == info) return 0;

    t = info;
    while (t < s) {
        if (isspace(*t) || iscntrl(*t)) return 0;
        t++;
    }
    return 1;
}

char const *
FindTrigInfo(Trigger *t, char const *header)
{
    TrigInfo *ti;
    size_t len;
    char const *s;

    if (!t || !header || !*header) return NULL;

    ti = t->infos;
    len = strlen(header);
    while(ti) {
        if (!strncasecmp(ti->info, header, len) &&
            ti->info[len] == ':') {
            s = ti->info + len + 1;
            while(isspace(*s)) s++;
            return s;
        }
        ti = ti->next;
    }
    return NULL;
}
