/***************************************************************/
/*                                                             */
/*  DOREM.C                                                    */
/*                                                             */
/*  Contains routines for parsing reminders and evaluating     */
/*  triggers.  Also contains routines for parsing OMIT         */
/*  commands.                                                  */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <stdlib.h>

#include "types.h"
#include "globals.h"
#include "err.h"
#include "protos.h"

static int ParseTimeTrig (ParsePtr s, TimeTrig *tim);
static int ParseLocalOmit (ParsePtr s, Trigger *t);
static int ParseScanFrom (ParsePtr s, Trigger *t, int type);
static int ParsePriority (ParsePtr s, Trigger *t);
static int ParseUntil (ParsePtr s, Trigger *t, int type);
static int ShouldTriggerBasedOnWarn (Trigger const *t, int dse, int *err);
static int ComputeTrigDuration(TimeTrig const *t);

static int
ensure_expr_references_first_local_arg(expr_node *node)
{
    expr_node *other;

    if (!node) {
        return 0;
    }
    if (node->type == N_LOCAL_VAR && node->u.arg == 0) {
        return 1;
    }
    if (ensure_expr_references_first_local_arg(node->child)) {
        return 1;
    }
    other = node->sibling;
    while (other) {
        if (ensure_expr_references_first_local_arg(other)) {
            return 1;
        }
        other = other->sibling;
    }
    return 0;
}

static void
check_trigger_function(char const *fname, char const *type)
{
    UserFunc *f;
    if (!*fname) {
        return;
    }
    f = FindUserFunc(fname);
    if (!f) {
        if (strcmp(type, "WARN")) {
            /* Undefined WARN functions are diagnosed elsewhere... */
            Wprint(tr("Undefined %s function: `%s'"), type, fname);
        }
        return;
    }
    if (f->nargs != 1) {
        Wprint(tr("%s function `%s' defined at %s(%s) should take 1 argument but actually takes %d"), type, fname, f->filename, line_range(f->lineno_start, f->lineno), f->nargs);
        return;
    }
    if (ensure_expr_references_first_local_arg(f->node)) {
        return;
    }
    Wprint(tr("%s function `%s' defined at %s(%s) does not use its argument"), type, fname, f->filename, line_range(f->lineno_start, f->lineno));
}

static void
ensure_satnode_mentions_trigdate_aux(expr_node *node, int *mentioned)
{
    char const *name;
    expr_node *other;
    UserFunc *f;

    if (!node) {
        return;
    }
    if (*mentioned) {
        return;
    }

    if (node->type == N_BUILTIN_FUNC) {
        name = node->u.builtin_func->name;
        if (!strcmp(name, "trigdate") ||
            !strcmp(name, "trigdatetime")) {
            *mentioned = 1;
            return;
        }
    } else if (node->type == N_SHORT_SYSVAR || node->type == N_SYSVAR) {
        if (node->type == N_SHORT_SYSVAR) {
            name = node->u.name;
        } else {
            name = node->u.value.v.str;
        }
        if (!StrCmpi(name, "T") ||
            !StrCmpi(name, "Td") ||
            !StrCmpi(name, "Tm") ||
            !StrCmpi(name, "Tw") ||
            !StrCmpi(name, "Ty")) {
            *mentioned = 1;
            return;
        }
    } else if (node->type == N_SHORT_USER_FUNC || node->type == N_USER_FUNC) {
        if (node->type == N_SHORT_USER_FUNC) {
            name = node->u.name;
        } else {
            name = node->u.value.v.str;
        }
        f = FindUserFunc(name);
        if (f && !f->recurse_flag) {
            f->recurse_flag = 1;
            ensure_satnode_mentions_trigdate_aux(f->node, mentioned);
            f->recurse_flag = 0;
            if (*mentioned) {
                return;
            }
        }
    }
    ensure_satnode_mentions_trigdate_aux(node->child, mentioned);
    if (*mentioned) {
        return;
    }
    other = node->sibling;
    while (other) {
        ensure_satnode_mentions_trigdate_aux(other, mentioned);
        if (*mentioned) {
            return;
        }
        other = other->sibling;
    }
}

static void ensure_satnode_mentions_trigdate(expr_node *node)
{
    int mentioned = 0;
    char const *str;
    if (node->type == N_CONSTANT || node->type == N_SHORT_STR) {
        if (node->type == N_CONSTANT) {
            if (node->u.value.type == INT_TYPE) {
                if (node->u.value.v.val == 0) {
                    Wprint(tr("SATISFY: constant 0 will never be true"));
                }
                return;
            }
            if (node->u.value.type != STR_TYPE) {
                return;
            }
            str = node->u.value.v.str;
        } else {
            str = node->u.name;
        }
        if (!*str) {
            Wprint(tr("SATISFY: constant \"\" will never be true"));
        }
        return;
    }

    ensure_satnode_mentions_trigdate_aux(node, &mentioned);
    if (!mentioned) {
        Wprint(tr("SATISFY: expression has no reference to trigdate() or $T..."));
    }
}


static int
ComputeTrigDuration(TimeTrig const *t)
{
    if (t->ttime == NO_TIME ||
        t->duration == NO_TIME) {
        return 0;
    }
    return (t->ttime + t->duration - 1) / MINUTES_PER_DAY;
}

/***************************************************************/
/*                                                             */
/*  DoRem                                                      */
/*                                                             */
/*  Do the REM command.                                        */
/*                                                             */
/***************************************************************/
int DoRem(ParsePtr p)
{

    Trigger trig;
    TimeTrig tim;
    int r, err;
    int dse;
    DynamicBuffer buf;
    Token tok;

    DBufInit(&buf);

    /* Parse the trigger date and time */
    if ( (r=ParseRem(p, &trig, &tim)) != OK ) {
        FreeTrig(&trig);
        return r;
    }

    if (trig.typ == NO_TYPE) {
        if (!Hush) {
            PurgeEchoLine("%s\n", "#!P! Cannot parse next line");
        }
        PurgeEchoLine("%s\n", CurLine);
        FreeTrig(&trig);
        return E_EOLN;
    }
    if (trig.typ == SAT_TYPE) {
        if (!Hush) {
            PurgeEchoLine("%s\n", "#!P: Cannot purge SATISFY-type reminders");
        }
        PurgeEchoLine("%s\n", CurLine);
        r=DoSatRemind(&trig, &tim, p);
        if (r) {
            if (r == E_CANT_TRIG && trig.maybe_uncomputable) {
                r = OK;
            }
            FreeTrig(&trig);
            if (r == E_EXPIRED) return OK;
            return r;
        }
        if (!LastTrigValid) {
            FreeTrig(&trig);
            return OK;
        }
        r=ParseToken(p, &buf);
        if (r) {
            FreeTrig(&trig);
            return r;
        }
        FindToken(DBufValue(&buf), &tok);
        DBufFree(&buf);
        if (tok.type == T_Empty || tok.type == T_Comment) {
            r = OK;
            if (trig.addomit) {
                r = AddGlobalOmit(LastTriggerDate);
            }
            DBufFree(&buf);
            FreeTrig(&trig);
            return r;
        }
        if (tok.type != T_RemType || tok.val == SAT_TYPE) {
            DBufFree(&buf);
            FreeTrig(&trig);
            return E_PARSE_ERR;
        }
        if (tok.val == PASSTHRU_TYPE) {
            r=ParseToken(p, &buf);
            if (r) {
                FreeTrig(&trig);
                return r;
            }
            if (!DBufLen(&buf)) {
                FreeTrig(&trig);
                DBufFree(&buf);
                return E_EOLN;
            }
            StrnCpy(trig.passthru, DBufValue(&buf), PASSTHRU_LEN);
            DBufFree(&buf);
        }
        trig.typ = tok.val;

        /* Convert some SPECIALs back to plain types */
        FixSpecialType(&trig);

        dse = LastTriggerDate;
        if (!LastTrigValid || PurgeMode) {
            FreeTrig(&trig);
            return OK;
        }
    } else {
        /* Calculate the trigger date */
        dse = ComputeTrigger(trig.scanfrom, &trig, &tim, &r, 1);
        if (r) {
            if (PurgeMode) {
                if (!Hush) {
                    PurgeEchoLine("%s: %s\n", "#!P! Problem calculating trigger date", GetErr(r));
                }
                PurgeEchoLine("%s\n", CurLine);
            }
            if (r == E_CANT_TRIG && trig.maybe_uncomputable) {
                r = OK;
            }
            FreeTrig(&trig);
            return r;
        }
    }

    /* Add to global OMITs if so indicated */
    if (trig.addomit) {
        r = AddGlobalOmit(dse);
        if (r) {
            FreeTrig(&trig);
            return r;
        }
    }
    if (PurgeMode) {
        if (trig.expired || dse < DSEToday) {
            if (p->expr_happened) {
                if (p->nonconst_expr) {
                    if (!Hush) {
                        PurgeEchoLine("%s\n", "#!P: Next line may have expired, but contains non-constant expression");
                        PurgeEchoLine("%s\n", "#!P: or a relative SCANFROM clause");
                    }
                    PurgeEchoLine("%s\n", CurLine);
                } else {
                    if (!Hush) {
                        PurgeEchoLine("%s\n", "#!P: Next line has expired, but contains expression...  please verify");
                    }
                    PurgeEchoLine("#!P: Expired: %s\n", CurLine);
                }
            } else {
                PurgeEchoLine("#!P: Expired: %s\n", CurLine);
            }
        } else {
            PurgeEchoLine("%s\n", CurLine);
        }
        FreeTrig(&trig);
        return OK;
    }

    /* Queue the reminder, if necessary */
    if (dse == DSEToday &&
        !(!IgnoreOnce &&
          trig.once != NO_ONCE &&
          GetOnceDate() == DSEToday))
        QueueReminder(p, &trig, &tim, trig.sched);
    /* If we're in daemon mode, do nothing over here */
    if (Daemon) {
        FreeTrig(&trig);
        return OK;
    }

    r = OK;
    if (ShouldTriggerReminder(&trig, &tim, dse, &err)) {
        if ( (r=TriggerReminder(p, &trig, &tim, dse, 0, NULL)) ) {
            FreeTrig(&trig);
            return r;
        }
    } else {
        /* Parse the rest of the line to catch any potential
           expression-pasting errors */
        if (ParseUntriggered) {
            while (ParseChar(p, &r, 0)) {
                if (r != 0) {
                    break;
                }
            }
        }
    }

    FreeTrig(&trig);
    return r;
}

/***************************************************************/
/*                                                             */
/*  ParseRem                                                   */
/*                                                             */
/*  Given a parse pointer, parse line and fill in a            */
/*  trigger structure.                                         */
/*                                                             */
/***************************************************************/
int ParseRem(ParsePtr s, Trigger *trig, TimeTrig *tim)
{
    register int r;
    DynamicBuffer buf;
    Token tok;
    int y, m, d;
    int seen_delta = 0;

    DBufInit(&buf);

    trig->y = NO_YR;
    trig->m = NO_MON;
    trig->d = NO_DAY;
    trig->wd = NO_WD;
    trig->back = NO_BACK;
    trig->delta = -DefaultDelta;
    trig->until = NO_UNTIL;
    trig->rep  = NO_REP;
    trig->localomit = NO_WD;
    trig->skip = NO_SKIP;
    trig->once = NO_ONCE;
    trig->addomit = 0;
    trig->noqueue = 0;
    trig->typ = NO_TYPE;
    trig->scanfrom = NO_DATE;
    trig->from = NO_DATE;
    trig->priority = DefaultPrio;
    trig->sched[0] = 0;
    trig->warn[0] = 0;
    trig->omitfunc[0] = 0;
    trig->duration_days = 0;
    trig->eventstart = NO_TIME;
    trig->eventduration = NO_TIME;
    trig->maybe_uncomputable = 0;
    DBufInit(&(trig->tags));
    trig->passthru[0] = 0;
    tim->ttime = NO_TIME;
    tim->delta = DefaultTDelta;
    tim->rep   = NO_REP;
    tim->duration = NO_TIME;
    trig->need_wkday = 0;
    trig->adj_for_last = 0;
    trig->infos = NULL;

    int parsing = 1;
    while(parsing) {
        /* Read space-delimited string */
        r = ParseToken(s, &buf);
        if (r) return r;

        /* Figure out what we've got */
        FindToken(DBufValue(&buf), &tok);
        switch(tok.type) {
        case T_In:
            /* Completely ignored */
            DBufFree(&buf);
            break;

        case T_Ordinal:
            DBufFree(&buf);
            if (trig->d != NO_DAY)     return E_DAY_TWICE;
            if (tok.val < 0) {
                if (trig->back != NO_BACK) return E_BACK_TWICE;
                trig->back = -7;
                trig->d = 1;
                trig->adj_for_last = 1;
            } else {
                trig->d = 1 + 7 * tok.val;
            }
            trig->need_wkday = 1;
            break;

        case T_Date:
            DBufFree(&buf);
            if (trig->d != NO_DAY) {
                return E_DAY_TWICE;
            }
            if (trig->m != NO_MON) {
                return E_MON_TWICE;
            }
            if (trig->y != NO_YR)  {
                return E_YR_TWICE;
            }

            FromDSE(tok.val, &y, &m, &d);
            trig->y = y;
            trig->m = m;
            trig->d = d;
            break;

        case T_DateTime:
            DBufFree(&buf);
            if (trig->d != NO_DAY) return E_DAY_TWICE;
            if (trig->m != NO_MON) return E_MON_TWICE;
            if (trig->y != NO_YR)  return E_YR_TWICE;
            FromDSE(tok.val / MINUTES_PER_DAY, &y, &m, &d);
            trig->y = y;
            trig->m = m;
            trig->d = d;
            tim->ttime = (tok.val % MINUTES_PER_DAY);
            break;

        case T_WkDay:
            DBufFree(&buf);
            if (trig->wd & (1 << tok.val)) return E_WD_TWICE;
            trig->wd |= (1 << tok.val);
            break;

        case T_Month:
            DBufFree(&buf);
            if (trig->m != NO_MON) return E_MON_TWICE;
            trig->m = tok.val;
            break;

        case T_MaybeUncomputable:
            DBufFree(&buf);
            trig->maybe_uncomputable = 1;
            break;

        case T_Skip:
            DBufFree(&buf);
            if (trig->skip != NO_SKIP) return E_SKIP_ERR;
            trig->skip = tok.val;
            break;

        case T_Priority:
            DBufFree(&buf);
            r=ParsePriority(s, trig);
            if (r) return r;
            break;

        /* A time implicitly introduces an AT if AT is not explicit */
        case T_Time:
            DBufFree(&buf);
            if (tim->ttime != NO_TIME) return E_TIME_TWICE;
            tim->ttime = tok.val;
            r = ParseTimeTrig(s, tim);
            if (r) return r;
            trig->duration_days = ComputeTrigDuration(tim);
            break;

        case T_At:
            DBufFree(&buf);
            r=ParseTimeTrig(s, tim);
            if (r) return r;
            trig->duration_days = ComputeTrigDuration(tim);
            break;

        case T_Scanfrom:
            DBufFree(&buf);
            r=ParseScanFrom(s, trig, tok.val);
            if (r) return r;
            break;

        case T_RemType:
            DBufFree(&buf);
            trig->typ = tok.val;
            if (s->isnested) return E_CANT_NEST_RTYPE;
            if (trig->typ == PASSTHRU_TYPE) {
                r = ParseToken(s, &buf);
                if (r) return r;
                if (!DBufLen(&buf)) {
                    DBufFree(&buf);
                    return E_EOLN;
                }
                StrnCpy(trig->passthru, DBufValue(&buf), PASSTHRU_LEN);
            }
            FixSpecialType(trig);
            parsing = 0;
            break;

        case T_Through:
            DBufFree(&buf);
            if (trig->rep != NO_REP) return E_REP_TWICE;
            trig->rep = 1;
            r = ParseUntil(s, trig, tok.type);
            if (r) return r;
            break;

        case T_Until:
            DBufFree(&buf);
            r=ParseUntil(s, trig, tok.type);
            if (r) return r;
            break;

        case T_Number:
            DBufFree(&buf);
            Eprint("`%d' is not recognized as a year (%d-%d) or a day number (1-31)",
                   tok.val, BASE, BASE+YR_RANGE);
            return E_PARSE_ERR;

        case T_Year:
            DBufFree(&buf);
            if (trig->y != NO_YR) return E_YR_TWICE;
            trig->y = tok.val;
            break;

        case T_Day:
            DBufFree(&buf);
            if (trig->d != NO_DAY) return E_DAY_TWICE;
            trig->d = tok.val;
            break;

        case T_Rep:
            DBufFree(&buf);
            if (trig->rep != NO_REP) return E_REP_TWICE;
            trig->rep = tok.val;
            break;

        case T_Delta:
            DBufFree(&buf);
            if (seen_delta) return E_DELTA_TWICE;
            seen_delta = 1;
            trig->delta = tok.val;
            break;

        case T_Back:
            DBufFree(&buf);
            if (trig->back != NO_BACK) return E_BACK_TWICE;
            trig->back = tok.val;
            break;

        case T_BackAdj:
            DBufFree(&buf);
            if (trig->back != NO_BACK) return E_BACK_TWICE;
            if (trig->d != NO_DAY) return E_DAY_TWICE;
            trig->back = tok.val;
            trig->d = 1;
            trig->adj_for_last = 1;
            break;

        case T_Once:
            DBufFree(&buf);
            if (trig->once != NO_ONCE) return E_ONCE_TWICE;
            trig->once = ONCE_ONCE;
            break;

        case T_AddOmit:
            DBufFree(&buf);
            trig->addomit = 1;
            break;

        case T_NoQueue:
            DBufFree(&buf);
            trig->noqueue = 1;
            break;

        case T_Omit:
            DBufFree(&buf);
            if (trig->omitfunc[0]) {
                Wprint(tr("Warning: OMIT is ignored if you use OMITFUNC"));
            }

            r = ParseLocalOmit(s, trig);
            if (r) return r;
            break;

        case T_Empty:
            DBufFree(&buf);
            parsing = 0;
            break;

        case T_OmitFunc:
            if (trig->localomit) {
                Wprint(tr("Warning: OMIT is ignored if you use OMITFUNC"));
            }
            r=ParseToken(s, &buf);
            if (r) return r;
            StrnCpy(trig->omitfunc, DBufValue(&buf), VAR_NAME_LEN);
            strtolower(trig->omitfunc);
            /* An OMITFUNC counts as a nonconst_expr! */
            s->expr_happened = 1;
            nonconst_debug(s->nonconst_expr, tr("OMITFUNC counts as a non-constant expression"));
            s->nonconst_expr = 1;
            DBufFree(&buf);
            break;

        case T_Warn:
            r=ParseToken(s, &buf);
            if(r) return r;
            StrnCpy(trig->warn, DBufValue(&buf), VAR_NAME_LEN);
            strtolower(trig->warn);
            DBufFree(&buf);
            break;

        case T_Info:
            r = ParseQuotedString(s, &buf);
            if (r != OK) {
                return r;
            }
            r = AppendTrigInfo(trig, DBufValue(&buf));
            DBufFree(&buf);
            if (r) return r;
            break;
        case T_Tag:
            r = ParseToken(s, &buf);
            if (r) return r;
            if (strchr(DBufValue(&buf), ',')) {
                DBufFree(&buf);
                return E_PARSE_ERR;
            }
            AppendTag(&(trig->tags), DBufValue(&buf));
            DBufFree(&buf);
            break;

        case T_Duration:
            r = ParseToken(s, &buf);
            if (r) return r;
            FindToken(DBufValue(&buf), &tok);
            DBufFree(&buf);
            switch(tok.type) {
            case T_Time:
            case T_LongTime:
            case T_Year:
            case T_Day:
            case T_Number:
                if (tok.val != 0) {
                    tim->duration = tok.val;
                } else {
                    tim->duration = NO_TIME;
                }
                trig->duration_days = ComputeTrigDuration(tim);
                break;
            default:
                return E_BAD_TIME;
            }
            break;

        case T_Sched:
            r=ParseToken(s, &buf);
            if(r) return r;
            StrnCpy(trig->sched, DBufValue(&buf), VAR_NAME_LEN);
            strtolower(trig->sched);
            DBufFree(&buf);
            break;

        case T_LongTime:
            DBufFree(&buf);
            return E_BAD_TIME;
            break;

        default:
            if (tok.type == T_Illegal && tok.val < 0) {
                Eprint("%s: `%s'", GetErr(-tok.val), DBufValue(&buf));
                DBufFree(&buf);
                return -tok.val;
            }
            PushToken(DBufValue(&buf), s);
            DBufFree(&buf);
            trig->typ = MSG_TYPE;
            if (s->isnested) return E_CANT_NEST_RTYPE;
            if (!WarnedAboutImplicit && !SuppressImplicitRemWarnings) {
                Wprint(tr("Missing REM type; assuming MSG"));
                WarnedAboutImplicit = 1;
            }
            parsing = 0;
            break;
        }
    }

    if (trig->need_wkday && trig->wd == NO_WD) {
        Eprint("Weekday name(s) required");
        return E_PARSE_ERR;
    }

    /* Adjust month and possibly year */
    if (trig->adj_for_last) {
        if (trig->m != NO_MON) {
            trig->m++;
            if (trig->m >= 12) {
                trig->m = 0;
                if (trig->y != NO_YR) {
                    trig->y++;
                }
            }
        }
        trig->adj_for_last = 0;
    }

    /* Check for some warning conditions */
    if (!s->nonconst_expr) {
        if (trig->y != NO_YR && trig->m != NO_MON && trig->d != NO_DAY && trig->until != NO_UNTIL) {
            if (DSE(trig->y, trig->m, trig->d) > trig->until) {
                Wprint(tr("Warning: UNTIL/THROUGH date earlier than start date"));
            }
        }
        if (trig->from != NO_DATE) {
            if (trig->until != NO_UNTIL && trig->until < trig->from) {
                Wprint(tr("Warning: UNTIL/THROUGH date earlier than FROM date"));
            }
        } else if (trig->scanfrom != NO_DATE) {
            if (trig->until != NO_UNTIL && trig->until < trig->scanfrom) {
                Wprint(tr("Warning: UNTIL/THROUGH date earlier than SCANFROM date"));
            }
        }
    }

    if (trig->y != NO_YR && trig->m != NO_MON && trig->d != NO_DAY && trig->until != NO_UNTIL && trig->rep == NO_REP) {
        Wprint(tr("Warning: Useless use of UNTIL with fully-specified date and no *rep"));
    }

    /* Set scanfrom to default if not set explicitly */
    if (trig->scanfrom == NO_DATE) {
        trig->scanfrom = DSEToday;
    }

    /* Check that any SCHED / WARN / OMITFUNC functions refer to
       their arguments */
    check_trigger_function(trig->sched, "SCHED");
    check_trigger_function(trig->warn, "WARN");
    check_trigger_function(trig->omitfunc, "OMITFUNC");
    return OK;
}

/***************************************************************/
/*                                                             */
/*  ParseTimeTrig - parse the AT part of a timed reminder      */
/*                                                             */
/***************************************************************/
static int ParseTimeTrig(ParsePtr s, TimeTrig *tim)
{
    Token tok;
    int r;
    int seen_delta = 0;
    DynamicBuffer buf;
    DBufInit(&buf);

    while(1) {
        r = ParseToken(s, &buf);
        if (r) return r;
        FindToken(DBufValue(&buf), &tok);
        switch(tok.type) {
        case T_Time:
            DBufFree(&buf);
            if (tim->ttime != NO_TIME) return E_TIME_TWICE;
            tim->ttime = tok.val;
            break;

        case T_Delta:
            DBufFree(&buf);
            if (seen_delta) return E_DELTA_TWICE;
            seen_delta = 1;
            tim->delta = (tok.val >= 0) ? tok.val : -tok.val;
            break;

        case T_Rep:
            DBufFree(&buf);
            if (tim->rep != NO_REP) return E_REP_TWICE;
            tim->rep = tok.val;
            break;

        default:
            if (tok.type == T_Illegal && tok.val < 0) {
                Eprint("%s: `%s'", GetErr(-tok.val), DBufValue(&buf));
                DBufFree(&buf);
                return -tok.val;
            }
            if (tim->ttime == NO_TIME) return E_EXPECT_TIME;

            PushToken(DBufValue(&buf), s);
            DBufFree(&buf);
            return OK;
        }
    }
}

/***************************************************************/
/*                                                             */
/*  ParseLocalOmit - parse the local OMIT portion of a         */
/*  reminder.                                                  */
/*                                                             */
/***************************************************************/
static int ParseLocalOmit(ParsePtr s, Trigger *t)
{
    Token tok;
    int r;
    DynamicBuffer buf;
    DBufInit(&buf);

    while(1) {
        r = ParseToken(s, &buf);
        if (r) return r;
        FindToken(DBufValue(&buf), &tok);
        switch(tok.type) {
        case T_WkDay:
            DBufFree(&buf);
            t->localomit |= (1 << tok.val);
            break;

        default:
            if (t->localomit == NO_WD) {
                return E_EXPECTING_WEEKDAY;
            }
            PushToken(DBufValue(&buf), s);
            DBufFree(&buf);
            return OK;
        }
    }
}

/***************************************************************/
/*                                                             */
/*  ParseUntil - parse the UNTIL portion of a reminder         */
/*                                                             */
/***************************************************************/
static int ParseUntil(ParsePtr s, Trigger *t, int type)
{
    int y = NO_YR,
        m = NO_MON,
        d = NO_DAY;

    char const *which;
    if (type == T_Until) {
        which = "UNTIL";
    } else {
        which = "THROUGH";
    }
    Token tok;
    int r;
    DynamicBuffer buf;
    DBufInit(&buf);

    if (t->until != NO_UNTIL) return E_UNTIL_TWICE;

    while(1) {
        r = ParseToken(s, &buf);
        if (r) return r;
        FindToken(DBufValue(&buf), &tok);
        switch(tok.type) {
        case T_Year:
            DBufFree(&buf);
            if (y != NO_YR) {
                Eprint("%s: %s", which, GetErr(E_YR_TWICE));
                return E_YR_TWICE;
            }
            y = tok.val;
            break;

        case T_Month:
            DBufFree(&buf);
            if (m != NO_MON) {
                Eprint("%s: %s", which, GetErr(E_MON_TWICE));
                return E_MON_TWICE;
            }
            m = tok.val;
            break;

        case T_Day:
            DBufFree(&buf);
            if (d != NO_DAY) {
                Eprint("%s: %s", which, GetErr(E_DAY_TWICE));
                return E_DAY_TWICE;
            }
            d = tok.val;
            break;

        case T_Date:
            DBufFree(&buf);
            if (y != NO_YR) {
                Eprint("%s: %s", which, GetErr(E_YR_TWICE));
                return E_YR_TWICE;
            }
            if (m != NO_MON) {
                Eprint("%s: %s", which, GetErr(E_MON_TWICE));
                return E_MON_TWICE;
            }
            if (d != NO_DAY) {
                Eprint("%s: %s", which, GetErr(E_DAY_TWICE));
                return E_DAY_TWICE;
            }
            FromDSE(tok.val, &y, &m, &d);
            break;

        default:
            if (tok.type == T_Illegal && tok.val < 0) {
                Eprint("%s: `%s'", GetErr(-tok.val), DBufValue(&buf));
                DBufFree(&buf);
                return -tok.val;
            }
            if (y == NO_YR || m == NO_MON || d == NO_DAY) {
                Eprint("%s: %s", which, GetErr(E_INCOMPLETE));
                DBufFree(&buf);
                return E_INCOMPLETE;
            }
            if (!DateOK(y, m, d)) {
                DBufFree(&buf);
                return E_BAD_DATE;
            }
            t->until = DSE(y, m, d);
            PushToken(DBufValue(&buf), s);
            DBufFree(&buf);
            return OK;
        }
    }
}

/***************************************************************/
/*                                                             */
/*  ParseScanFrom - parse the FROM/SCANFROM portion            */
/*                                                             */
/***************************************************************/
static int ParseScanFrom(ParsePtr s, Trigger *t, int type)
{
    int y = NO_YR,
        m = NO_MON,
        d = NO_DAY;

    Token tok;
    int r;
    DynamicBuffer buf;
    char const *word;

    DBufInit(&buf);
    if (type == SCANFROM_TYPE) {
        word = "SCANFROM";
    } else {
        word = "FROM";
    }

    if (t->scanfrom != NO_DATE) return E_SCAN_TWICE;

    while(1) {
        r = ParseToken(s, &buf);
        if (r) return r;
        FindToken(DBufValue(&buf), &tok);
        switch(tok.type) {
        case T_Year:
            DBufFree(&buf);
            if (y != NO_YR) {
                Eprint("%s: %s", word, GetErr(E_YR_TWICE));
                return E_YR_TWICE;
            }
            y = tok.val;
            break;

        case T_Month:
            DBufFree(&buf);
            if (m != NO_MON) {
                Eprint("%s: %s", word, GetErr(E_MON_TWICE));
                return E_MON_TWICE;
            }
            m = tok.val;
            break;

        case T_Day:
            DBufFree(&buf);
            if (d != NO_DAY) {
                Eprint("%s: %s", word, GetErr(E_DAY_TWICE));
                return E_DAY_TWICE;
            }
            d = tok.val;
            break;

        case T_Date:
            DBufFree(&buf);
            if (y != NO_YR) {
                Eprint("%s: %s", word, GetErr(E_YR_TWICE));
                return E_YR_TWICE;
            }
            if (m != NO_MON) {
                Eprint("%s: %s", word, GetErr(E_MON_TWICE));
                return E_MON_TWICE;
            }
            if (d != NO_DAY) {
                Eprint("%s: %s", word, GetErr(E_DAY_TWICE));
                return E_DAY_TWICE;
            }
            FromDSE(tok.val, &y, &m, &d);
            break;

        case T_Back:
            DBufFree(&buf);
            if (type != SCANFROM_TYPE) {
                Eprint("%s: %s", word, GetErr(E_INCOMPLETE));
                return E_INCOMPLETE;
            }
            if (y != NO_YR) {
                Eprint("%s: %s", word, GetErr(E_YR_TWICE));
                return E_YR_TWICE;
            }
            if (m != NO_MON) {
                Eprint("%s: %s", word, GetErr(E_MON_TWICE));
                return E_MON_TWICE;
            }
            if (d != NO_DAY) {
                Eprint("%s: %s", word, GetErr(E_DAY_TWICE));
                return E_DAY_TWICE;
            }
            if (tok.val < 0) {
                tok.val = -tok.val;
            }
            FromDSE(DSEToday - tok.val, &y, &m, &d);
            /* Don't purge reminders with a relative scanfrom */
            s->expr_happened = 1;
            nonconst_debug(s->nonconst_expr, tr("Relative SCANFROM counts as a non-constant expression"));
            s->nonconst_expr = 1;
            break;

        default:
            if (tok.type == T_Illegal && tok.val < 0) {
                Eprint("%s: `%s'", GetErr(-tok.val), DBufValue(&buf));
                DBufFree(&buf);
                return -tok.val;
            }
            if (y == NO_YR || m == NO_MON || d == NO_DAY) {
                Eprint("%s: %s", word, GetErr(E_INCOMPLETE));
                DBufFree(&buf);
                return E_INCOMPLETE;
            }
            if (!DateOK(y, m, d)) {
                DBufFree(&buf);
                return E_BAD_DATE;
            }
            t->scanfrom = DSE(y, m, d);
            if (type == FROM_TYPE) {
                t->from = t->scanfrom;
                if (t->scanfrom < DSEToday) {
                    t->scanfrom = DSEToday;
                }
            } else {
                t->from = NO_DATE;
            }

            PushToken(DBufValue(&buf), s);
            DBufFree(&buf);
            return OK;
        }
    }
}


/***************************************************************/
/*                                                             */
/*  TriggerReminder                                            */
/*                                                             */
/*  Trigger the reminder if it's a RUN or MSG type.            */
/*                                                             */
/***************************************************************/
int TriggerReminder(ParsePtr p, Trigger *t, TimeTrig const *tim, int dse, int is_queued, DynamicBuffer *output)
{
    int r, y, m, d;
    int adjusted_for_newline = 0;
    char PrioExpr[VAR_NAME_LEN+25];
    char tmpBuf[64];
    DynamicBuffer buf, calRow;
    DynamicBuffer pre_buf;
    char const *s;
    char const *msg_command = NULL;
    Value v;

    if (MsgCommand) {
        msg_command = MsgCommand;
    }
    if (is_queued && QueuedMsgCommand) {
        msg_command = QueuedMsgCommand;
    }

    /* A null command is no command */
    if (msg_command && !*msg_command) {
        msg_command = NULL;
    }

    int red = -1, green = -1, blue = -1;
    int is_color = 0;

    DBufInit(&buf);
    DBufInit(&calRow);
    DBufInit(&pre_buf);
    if (t->typ == RUN_TYPE && RunDisabled) return E_RUN_DISABLED;
    if ((t->typ == PASSTHRU_TYPE && StrCmpi(t->passthru, "COLOR") && StrCmpi(t->passthru, "COLOUR")) ||
        t->typ == CAL_TYPE ||
        t->typ == PS_TYPE ||
        t->typ == PSF_TYPE)
        return OK;

    /* Handle COLOR types */
    if (t->typ == PASSTHRU_TYPE && (!StrCmpi(t->passthru, "COLOR") || !StrCmpi(t->passthru, "COLOUR"))) {
        /* Strip off three tokens */
        r = ParseToken(p, &buf);
        sscanf(DBufValue(&buf), "%d", &red);
        if (!NextMode) {
            DBufPuts(&pre_buf, DBufValue(&buf));
            DBufPutc(&pre_buf, ' ');
        }
        DBufFree(&buf);
        if (r) return r;
        r = ParseToken(p, &buf);
        sscanf(DBufValue(&buf), "%d", &green);
        if (!NextMode) {
            DBufPuts(&pre_buf, DBufValue(&buf));
            DBufPutc(&pre_buf, ' ');
        }
        DBufFree(&buf);
        if (r) return r;
        r = ParseToken(p, &buf);
        sscanf(DBufValue(&buf), "%d", &blue);
        if (!NextMode) {
            DBufPuts(&pre_buf, DBufValue(&buf));
            DBufPutc(&pre_buf, ' ');
        }
        DBufFree(&buf);
        if (r) return r;
        t->typ = MSG_TYPE;
    }
/* If it's a MSG-type reminder, and no -k option was used, issue the banner. */
    if ((t->typ == MSG_TYPE || t->typ == MSF_TYPE) 
        && !DidMsgReminder && !NextMode && !msg_command && !is_queued) {
        DidMsgReminder = 1;
        if (!DoSubstFromString(DBufValue(&Banner), &buf,
                               DSEToday, NO_TIME) &&
            DBufLen(&buf)) {
            printf("%s\n", DBufValue(&buf));
        }
        DBufFree(&buf);
    }

/* If it's NextMode, process as a ADVANCE_MODE-type entry, and issue
   simple-calendar format. */
    if (NextMode) {
        if ( (r=DoSubst(p, &buf, t, tim, dse, ADVANCE_MODE)) ) return r;
        if (!DBufLen(&buf)) {
            DBufFree(&buf);
            DBufFree(&pre_buf);
            return OK;
        }
        FromDSE(dse, &y, &m, &d);
        snprintf(tmpBuf, sizeof(tmpBuf), "%04d/%02d/%02d ", y, m+1, d);
        if (DBufPuts(&calRow, tmpBuf) != OK) {
            DBufFree(&calRow);
            DBufFree(&pre_buf);
            return E_NO_MEM;
        }
        /* If DoSimpleCalendar==1, output *all* simple calendar fields */
        if (DoSimpleCalendar) {
            /* ignore passthru field when in NextMode */
            if (DBufPuts(&calRow, "* ") != OK) {
                DBufFree(&calRow);
                DBufFree(&pre_buf);
                return E_NO_MEM;
            }
            if (*DBufValue(&(t->tags))) {
                DBufPuts(&calRow, DBufValue(&(t->tags)));
                DBufPutc(&calRow, ' ');
            } else {
                DBufPuts(&calRow, "* ");
            }
            if (tim->duration != NO_TIME) {
                snprintf(tmpBuf, sizeof(tmpBuf), "%d ", tim->duration);
            } else {
                snprintf(tmpBuf, sizeof(tmpBuf), "* ");
            }
            if (DBufPuts(&calRow, tmpBuf) != OK) {
                DBufFree(&calRow);
                DBufFree(&pre_buf);
                return E_NO_MEM;
            }
            if (tim->ttime != NO_TIME) {
                snprintf(tmpBuf, sizeof(tmpBuf), "%d ", tim->ttime);
            } else {
                snprintf(tmpBuf, sizeof(tmpBuf), "* ");
            }
            if (DBufPuts(&calRow, tmpBuf) != OK) {
                DBufFree(&calRow);
                DBufFree(&pre_buf);
                return E_NO_MEM;
            }
        }
        if (DBufPuts(&calRow, SimpleTime(tim->ttime)) != OK) {
            DBufFree(&calRow);
            DBufFree(&pre_buf);
            return E_NO_MEM;
        }

        r = OK;
        if (output) {
            if (DBufPuts(output, DBufValue(&calRow)) != OK) r = E_NO_MEM;
            if (DBufPuts(output, DBufValue(&pre_buf)) != OK) r = E_NO_MEM;
            if (DBufPuts(output, DBufValue(&buf)) != OK) r = E_NO_MEM;
        } else {
            printf("%s%s%s\n", DBufValue(&calRow), DBufValue(&pre_buf), DBufValue(&buf));
        }
        DBufFree(&buf);
        DBufFree(&pre_buf);
        DBufFree(&calRow);
        return r;
    }

    /* Correct colors */
    if (UseVTColors) {
        if (red == -1 && green == -1 && blue == -1) {
            if (DefaultColorR != -1 && DefaultColorG != -1 && DefaultColorB != -1) {
                red = DefaultColorR;
                green = DefaultColorG;
                blue = DefaultColorB;
            }
        }
        if (red >= 0 && green >= 0 && blue >= 0) {
            is_color = 1;
            if (red > 255) red = 255;
            if (green > 255) green = 255;
            if (blue > 255) blue = 255;
        }
    }

    /* Put the substituted string into the substitution buffer */

    /* Don't use msgprefix() on RUN-type reminders */
    if (t->typ != RUN_TYPE) {
        if (UserFuncExists("msgprefix") == 1) {
            snprintf(PrioExpr, sizeof(PrioExpr), "msgprefix(%d)", t->priority);
            s = PrioExpr;
            r = EvalExpr(&s, &v, NULL);
            if (!r) {
                if (!DoCoerce(STR_TYPE, &v)) {
                    if (is_color) {
                        DBufPuts(&buf, Colorize(red, green, blue, 0, 1));
                    }
                    if (DBufPuts(&buf, v.v.str) != OK) {
                        DBufFree(&buf);
                        DestroyValue(v);
                        return E_NO_MEM;
                    }
                }
                DestroyValue(v);
            }
        }
    }

    if (is_color) {
        DBufPuts(&buf, Colorize(red, green, blue, 0, 1));
    }
    if ( (r=DoSubst(p, &buf, t, tim, dse, NORMAL_MODE)) ) return r;

    if (t->typ != RUN_TYPE) {
        if (UserFuncExists("msgsuffix") == 1) {
            snprintf(PrioExpr, sizeof(PrioExpr), "msgsuffix(%d)", t->priority);
            s = PrioExpr;
            r = EvalExpr(&s, &v, NULL);
            if (!r) {
                if (!DoCoerce(STR_TYPE, &v)) {
                    /* Hack: If MsgSuffix starts with a backspace, move it before the newline! */
                    if (*v.v.str == '\b') {
                        if (DBufLen(&buf)) {
                            if (DBufValue(&buf)[DBufLen(&buf)-1] == '\n') {
                                /* VIOLATION of encapsulation! */
                                DBufValue(&buf)[DBufLen(&buf)-1] = 0;
                                buf.len--;
                                adjusted_for_newline = 1;
                            }
                        }
                    }
                    if (is_color) {
                        DBufPuts(&buf, Colorize(red, green, blue, 0, 1));
                    }
                    if (*v.v.str == '\b') {
                        r = DBufPuts(&buf, v.v.str+1);
                    } else {
                        r = DBufPuts(&buf, v.v.str);
                    }
                    if (r != OK) {
                        DBufFree(&buf);
                        DestroyValue(v);
                        return E_NO_MEM;
                    }
                }
                DestroyValue(v);
            }
        }
    }

    if (is_color) {
        DBufPuts(&buf, Decolorize());
    }
    if (adjusted_for_newline) {
        DBufPutc(&buf, '\n');
    }

    if ((!msg_command && t->typ == MSG_TYPE) || t->typ == MSF_TYPE) {
        if (DBufPutc(&buf, '\n') != OK) {
            DBufFree(&buf);
            return E_NO_MEM;
        }
    }

    /* If this is a dupe and we are de-duping, do nothing */
    if (DedupeReminders) {
        if (ShouldDedupe(dse, tim->ttime, DBufValue(&buf))) {
            DBufFree(&buf);
            return OK;
        }
    }

/* If we are sorting, just queue it up in the sort buffer */
    if (SortByDate) {
        if (InsertIntoSortBuffer(dse, tim->ttime, DBufValue(&buf),
                                 t->typ, t->priority) == OK) {
            DBufFree(&buf);
            NumTriggered++;
            return OK;
        }
    }

/* If we didn't insert the reminder into the sort buffer, issue the
   reminder now. */
    switch(t->typ) {
    case MSG_TYPE:
    case PASSTHRU_TYPE:
        if (msg_command) {
            DoMsgCommand(msg_command, DBufValue(&buf), is_queued);
        } else {
            if (output) {
                DBufPuts(output, DBufValue(&buf));
            } else {
                /* Add a space before "NOTE endreminder" */
                if (IsServerMode() && !strncmp(DBufValue(&buf), "NOTE endreminder", 16)) {
                    printf(" %s", DBufValue(&buf));
                } else {
                    printf("%s", DBufValue(&buf));
                }
            }
        }
        break;

    case MSF_TYPE:
        FillParagraph(DBufValue(&buf), output);
        break;

    case RUN_TYPE:
        System(DBufValue(&buf), is_queued);
        break;

    default: /* Unknown/illegal type? */
        DBufFree(&buf);
        return E_SWERR;
    }

    DBufFree(&buf);
    NumTriggered++;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  ShouldTriggerReminder                                      */
/*                                                             */
/*  Return 1 if we should trigger a reminder, based on today's */
/*  date and the trigger.  Return 0 if reminder should not be  */
/*  triggered.  Sets *err non-zero in event of an error.       */
/*                                                             */
/***************************************************************/
int ShouldTriggerReminder(Trigger const *t, TimeTrig const *tim, int dse, int *err)
{
    int r, omit;
    *err = 0;

    /* Handle the ONCE modifier in the reminder. */
    if (!IgnoreOnce && t->once !=NO_ONCE && GetOnceDate() == DSEToday)
        return 0;

    if (dse < DSEToday) return 0;

    /* Don't trigger timed reminders if DontIssueAts is true, and if the
       reminder is for today */
    if (dse == DSEToday && DontIssueAts && tim->ttime != NO_TIME) {
        if (DontIssueAts > 1) {
            /* If two or more -a options, then *DO* issue ats that are in the
               future */
            if (tim->ttime < MinutesPastMidnight(0)) {
                return 0;
            }
        } else {
            return 0;
        }
    }

    /* If "infinite delta" option is chosen, always trigger future reminders */
    if (InfiniteDelta || NextMode) return 1;

    /* If there's a "warn" function, it overrides any deltas except
     * DeltaOverride*/
    if (t->warn[0] != 0) {
        if (DeltaOverride > 0) {
            if (dse <= DSEToday + DeltaOverride) {
                return 1;
            }
        }
        return ShouldTriggerBasedOnWarn(t, dse, err);
    }

    /* Zero delta */
    if (DeltaOverride < 0) {
        return dse == DSEToday;
    }

    /* Move back by delta days, if any */
    if (DeltaOverride) {
        /* A positive DeltaOverride takes precedence over everything */
        dse = dse - DeltaOverride;
    } else if (t->delta != NO_DELTA) {
        if (t->delta < 0)
            dse = dse + t->delta;
        else {
            int iter = 0;
            int max = MaxSatIter;
            r = t->delta;
            if (max < r*2) max = r*2;
            while(iter++ < max) {
                if (!r || (dse <= DSEToday)) {
                    break;
                }
                dse--;
                *err = IsOmitted(dse, t->localomit, t->omitfunc, &omit);
                if (*err) return 0;
                if (!omit) r--;
            }
            if (iter > max) {
                *err = E_CANT_TRIG;
                Eprint("Delta: Bad OMITFUNC? %s", GetErr(E_CANT_TRIG));
                return 0;
            }
        }
    }

    /* Should we trigger the reminder? */
    return (dse <= DSEToday);
}

/***************************************************************/
/*                                                             */
/*  DoSatRemind                                                */
/*                                                             */
/*  Do the "satisfying..." remind calculation.                 */
/*                                                             */
/***************************************************************/
int DoSatRemind(Trigger *trig, TimeTrig *tt, ParsePtr p)
{
    int iter, dse, r, start;
    Value v;
    expr_node *sat_node;
    int nonconst = 0;

    sat_node = ParseExpr(p, &r);
    if (r != OK) {
        return r;
    }
    if (!sat_node) {
        return E_SWERR;
    }

    /* Diagnose if SAT_NODE does not reference trigdate */
    ensure_satnode_mentions_trigdate(sat_node);

    iter = 0;
    start = trig->scanfrom;
    while (iter++ < MaxSatIter) {
        dse = ComputeTriggerNoAdjustDuration(start, trig, tt, &r, 1, 0);
        if (r) {
            free_expr_tree(sat_node);
            if (r == E_CANT_TRIG) return OK; else return r;
        }
        if (dse != start && trig->duration_days) {
            dse = ComputeTriggerNoAdjustDuration(start, trig, tt, &r, 1, trig->duration_days);
            if (r) {
                free_expr_tree(sat_node);
                if (r == E_CANT_TRIG) return OK; else return r;
            }
        } else if (dse == start) {
            if (tt->ttime != NO_TIME) {
                trig->eventstart = MINUTES_PER_DAY * r + tt->ttime;
                if (tt->duration != NO_TIME) {
                    trig->eventduration = tt->duration;
                }
            }
            SaveAllTriggerInfo(trig, tt, dse, tt->ttime, 1);
        }
        if (dse == -1) {
            free_expr_tree(sat_node);
            LastTrigValid = 0;
            LastTriggerDate = -1;
            return E_EXPIRED;
        }
        r = evaluate_expression(sat_node, NULL, &v, &nonconst);
        if (r) {
            free_expr_tree(sat_node);
            return r;
        }
        if (v.type != INT_TYPE && v.type != STR_TYPE) {
            free_expr_tree(sat_node);
            return E_BAD_TYPE;
        }
        if ((v.type == INT_TYPE && v.v.val) ||
            (v.type == STR_TYPE && *v.v.str)) {
            AdjustTriggerForDuration(trig->scanfrom, dse, trig, tt, 1);
            if (DebugFlag & DB_PRTTRIG) {
                int y, m, d;
                FromDSE(LastTriggerDate, &y, &m, &d);
                fprintf(ErrFp, "%s(%s): Trig(satisfied) = %s, %d %s, %d",
                        GetCurrentFilename(), line_range(LineNoStart, LineNo),
                        get_day_name(LastTriggerDate % 7),
                        d,
                        get_month_name(m),
                        y);
                if (tt->ttime != NO_TIME) {
                    fprintf(ErrFp, " AT %02d:%02d",
                            (tt->ttime / 60),
                            (tt->ttime % 60));
                    if (tt->duration != NO_TIME) {
                        fprintf(ErrFp, " DURATION %02d:%02d",
                                (tt->duration / 60),
                                (tt->duration % 60));
                    }
                }
                fprintf(ErrFp, "\n");
            }
            free_expr_tree(sat_node);
            return OK;
        }
        if (dse+trig->duration_days < start) {
            start++;
        } else {
            start = dse+trig->duration_days+1;
        }
    }
    LastTrigValid = 0;
    free_expr_tree(sat_node);
    return E_CANT_TRIG;
}

/***************************************************************/
/*                                                             */
/*  ParsePriority - parse the PRIORITY portion of a reminder   */
/*                                                             */
/***************************************************************/
static int ParsePriority(ParsePtr s, Trigger *t)
{
    int p, r;
    char const *u;
    DynamicBuffer buf;
    DBufInit(&buf);

    r = ParseToken(s, &buf);
    if(r) return r;
    u = DBufValue(&buf);

    if (!isdigit(*u)) {
        DBufFree(&buf);
        return E_EXPECTING_NUMBER;
    }
    p = 0;
    while (isdigit(*u)) {
        p = p*10 + *u - '0';
        u++;
    }
    if (*u) {
        DBufFree(&buf);
        return E_EXPECTING_NUMBER;
    }

    DBufFree(&buf);

/* Tricky!  The only way p can be < 0 is if overflow occurred; thus,
   E2HIGH is indeed the appropriate error message. */
    if (p<0 || p>9999) return E_2HIGH;
    t->priority = p;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  DoMsgCommand                                               */
/*                                                             */
/*  Execute the '-k' command, escaping shell chars in message. */
/*                                                             */
/***************************************************************/
int DoMsgCommand(char const *cmd, char const *msg, int is_queued)
{
    int r;
    int i, l;
    DynamicBuffer execBuffer;

    DynamicBuffer buf;

    DBufInit(&buf);
    DBufInit(&execBuffer);

    /* Escape shell characters in msg */
    if (ShellEscape(msg, &buf) != OK) {
        r = E_NO_MEM;
        goto finished;
    }

    msg = DBufValue(&buf);

    /* Do "%s" substitution */
    l = strlen(cmd);
    for (i=0; i<l; i++) {
        if (cmd[i] == '%' && cmd[i+1] == 's') {
            ++i;
            if (DBufPuts(&execBuffer, msg) != OK) {
                r = E_NO_MEM;
                goto finished;
            }
        } else {
            if (DBufPutc(&execBuffer, cmd[i]) != OK) {
                r = E_NO_MEM;
                goto finished;
            }
        }
    }
    r = OK;

    System(DBufValue(&execBuffer), is_queued);

finished:
    DBufFree(&buf);
    DBufFree(&execBuffer);
    return r;
}

/***************************************************************/
/*                                                             */
/*  ShouldTriggerBasedOnWarn                                   */
/*                                                             */
/*  Determine whether to trigger a reminder based on its WARN  */
/*  function.                                                  */
/*                                                             */
/***************************************************************/
static int ShouldTriggerBasedOnWarn(Trigger const *t, int dse, int *err)
{
    char buffer[VAR_NAME_LEN+32];
    int i;
    char const *s;
    int r, omit;
    Value v;
    int lastReturnVal = 0; /* Silence compiler warning */

    /* If no proper function exists, barf... */
    if (UserFuncExists(t->warn) != 1) {
        Eprint("%s: `%s'", GetErr(M_BAD_WARN_FUNC), t->warn);
        return (dse == DSEToday);
    }
    for (i=1; ; i++) {
        snprintf(buffer, sizeof(buffer), "%s(%d)", t->warn, i);
        s = buffer;
        r = EvalExpr(&s, &v, NULL);
        if (r) {
            Eprint("%s: `%s': %s", GetErr(M_BAD_WARN_FUNC),
                   t->warn, GetErr(r));
            return (dse == DSEToday);
        }
        if (v.type != INT_TYPE) {
            DestroyValue(v);
            Eprint("%s: `%s': %s", GetErr(M_BAD_WARN_FUNC),
                   t->warn, GetErr(E_BAD_TYPE));
            return (dse == DSEToday);
        }

        /* If absolute value of return is not monotonically
           decreasing, exit */
        if (i > 1 && abs(v.v.val) >= lastReturnVal) {
            return (dse == DSEToday);
        }

        lastReturnVal = abs(v.v.val);
        /* Positive values: Just subtract.  Negative values:
           skip omitted days. */
        if (v.v.val >= 0) {
            if (DSEToday + v.v.val == dse) return 1;
        } else {
            int j = dse;
            int iter = 0;
            int max = MaxSatIter;
            if (max < v.v.val * 2) max = v.v.val*2;
            while(iter++ <= max) {
                j--;
                *err = IsOmitted(j, t->localomit, t->omitfunc, &omit);
                if (*err) return 0;
                if (!omit) v.v.val++;
                if (!v.v.val) {
                    break;
                }
            }
            if (iter > max) {
                Eprint("Delta: Bad OMITFUNC? %s", GetErr(E_CANT_TRIG));
                return 0;
            }
            if (j == DSEToday) return 1;
        }
    }
}

void FixSpecialType(Trigger *t)
{
    if (t->typ != PASSTHRU_TYPE) {
        return;
    }

    /* Convert SPECIAL MSG / MSF / RUN / CAL to just plain MSG / MSF / etc */
    if (!StrCmpi(t->passthru, "MSG")) {
        t->typ = MSG_TYPE;
    } else if (!StrCmpi(t->passthru, "MSF")) {
        t->typ = MSF_TYPE;
    } else if (!StrCmpi(t->passthru, "RUN")) {
        t->typ = RUN_TYPE;
    } else if (!StrCmpi(t->passthru, "CAL")) {
        t->typ = CAL_TYPE;
    } else if (!StrCmpi(t->passthru, "PS")) {
        t->typ = PS_TYPE;
    } else if (!StrCmpi(t->passthru, "PSFILE")) {
        t->typ = PSF_TYPE;
    }
}
