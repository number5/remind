/***************************************************************/
/*                                                             */
/*  DOSUBST.C                                                  */
/*                                                             */
/*  This performs all the "%" substitution functions when      */
/*  reminders are triggered.                                   */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <stdlib.h>

#include "globals.h"
#include "err.h"
#include "protos.h"

#define UPPER(c) (islower(c) ? toupper(c) : (c))
#define ABS(x) ( (x) < 0 ? -(x) : (x) )
#ifndef NL
#define NL "\n"
#endif

#define SHIP_OUT(s) if(DBufPuts(dbuf, s) != OK) return E_NO_MEM

static char const *
get_function_override(int c, int addx)
{
    static char func[32];

    if (isalnum(c) || c == '_') {
        if (addx) {
            snprintf(func, sizeof(func), "subst_%cx", tolower(c));
        } else {
            snprintf(func, sizeof(func), "subst_%c", tolower(c));
        }
        return func;
    }
    if (addx) {
        switch(c) {
        case ':': return "subst_colonx";
        case '!': return "subst_bangx";
        case '?': return "subst_questionx";
        case '@': return "subst_atx";
        case '#': return "subst_hashx";
        }
    } else {
        switch(c) {
        case ':': return "subst_colon";
        case '!': return "subst_bang";
        case '?': return "subst_question";
        case '@': return "subst_at";
        case '#': return "subst_hash";
        }
    }
    return NULL;
}

static int
check_subst_args(UserFunc *f, int n)
{
    if (!f) {
        return 0;
    }
    if (f->nargs == n) {
        return 1;
    }
    Wprint(tr("Function `%s' defined at %s(%s) should take %d argument%s, but actually takes %d"),
           f->name, f->filename, line_range(f->lineno_start, f->lineno), n, (n == 1 ? "" : "s"), f->nargs);
    return 0;
}
/***************************************************************/
/*                                                             */
/*  DoSubst                                                    */
/*                                                             */
/*  Process the % escapes in the reminder.  If                 */
/*  mode==NORMAL_MODE, ignore the %" sequence.  If             */
/*  mode==CAL_MODE, process the %" sequence.                   */
/*  If mode==ADVANCE_MODE, ignore %" but don't add newline     */
/*                                                             */
/***************************************************************/
int DoSubst(ParsePtr p, DynamicBuffer *dbuf, Trigger *t, TimeTrig const *tt, int dse, int mode)
{
    int diff = dse - DSEToday;
    int rdiff = dse - RealToday;
    int bangdiff = diff;
    int curtime = MinutesPastMidnight(0);
    int err, done;
    int c;
    int d, m, y;
    int tim = NO_TIME;
    int h, min, hh, ch, cmin, chh;
    int i;
    char const *pm, *cpm;
    int tdiff, adiff, mdiff, hdiff;
    char const *mplu, *hplu, *when, *plu;
    char const *is, *was;
    int has_quote = 0;
    char *ss;
    char const *expr;
    char *os;
    char s[256];
    char const *substname;
    char mypm[64];
    char mycpm[64];
    char myplu[64];
    int origLen = DBufLen(dbuf);
    int altmode;
    int r;
    int origtime;
    Value v;
    UserFunc *func;

    FromDSE(dse, &y, &m, &d);

    if (tt) {
        tim = tt->ttime;
    }
    origtime = tim;
    if (tim == NO_TIME) tim = curtime;
    tdiff = tim - curtime;
    adiff = ABS(tdiff);
    mdiff = adiff % 60;
    hdiff = adiff / 60;

    mplu = (mdiff == 1 ? "" : DynamicMplu);
    hplu = (hdiff == 1 ? "" : DynamicHplu);

    when = (tdiff < 0) ? tr("ago") :
                         tr("from now");

    h = tim / 60;
    min = tim % 60;

    r = -1;
    func = FindUserFunc("subst_ampm");
    if (func && check_subst_args(func, 1)) {
        snprintf(s, sizeof(s), "subst_ampm(%d)", h);
        expr = (char const *) s;
        r = EvalExprRunDisabled(&expr, &v, NULL);
        if (r == OK) {
            if (!DoCoerce(STR_TYPE, &v)) {
                snprintf(mypm, sizeof(mypm), "%s", v.v.str);
                pm = mypm;
            } else {
                r = -1;
            }
            DestroyValue(v);
        } else {
            Eprint("%s", GetErr(r));
        }
    }
    if (r != OK) {
        pm = (h < 12) ? tr("am") :
                        tr("pm");
    }

    hh = (h == 12 || h == 0) ? 12 : h % 12;

    ch = curtime / 60;
    cmin = curtime % 60;

    r = -1;
    func = FindUserFunc("subst_ampm");
    if (func && check_subst_args(func, 1)) {
        snprintf(s, sizeof(s), "subst_ampm(%d)", ch);
        expr = (char const *) s;
        r = EvalExprRunDisabled(&expr, &v, NULL);
        if (r == OK) {
            if (!DoCoerce(STR_TYPE, &v)) {
                snprintf(mycpm, sizeof(mycpm), "%s", v.v.str);
                cpm = mycpm;
            } else {
                r = -1;
            }
            DestroyValue(v);
        } else {
            Eprint("%s", GetErr(r));
        }
    }
    if (r != OK) {
        cpm = (h < 12) ? tr("am") :
                         tr("pm");
    }
    chh = (ch == 0 || ch == 12) ? 12 : ch % 12;

    func = FindUserFunc("subst_ordinal");
    if (func && check_subst_args(func, 1)) {
        snprintf(s, sizeof(s), "subst_ordinal(%d)", d);
        expr = (char const *) s;
        r = EvalExprRunDisabled(&expr, &v, NULL);
        if (r == OK) {
            if (!DoCoerce(STR_TYPE, &v)) {
                snprintf(myplu, sizeof(myplu), "%s", v.v.str);
                plu = myplu;
            } else {
                r = -1;
            }
            DestroyValue(v);
        } else {
            Eprint("%s", GetErr(r));
        }
    }
    if (r != OK) {
        switch(d) {
        case 1:
        case 21:
        case 31: plu = "st"; break;

        case 2:
        case 22: plu = "nd"; break;

        case 3:
        case 23: plu = "rd"; break;

        default: plu = "th"; break;
        }
    }

    while(1) {
        c = ParseChar(p, &err, 0);
        if (err) {
            DBufFree(dbuf);
            return err;
        }
        if (c == '\n') continue;
        if (!c) {
            if (AddBlankLines &&
                mode != CAL_MODE &&
                mode != ADVANCE_MODE &&
                t->typ != RUN_TYPE &&
                !(MsgCommand && *MsgCommand)) {
                if (DBufPutc(dbuf, '\n') != OK) return E_NO_MEM;
            }
            break;
        }
        if (c != '%') {
            if (DBufPutc(dbuf, c) != OK) return E_NO_MEM;
            continue;
        }
        altmode = 0;
        s[0] = 0;
        c = ParseChar(p, &err, 0);
        if (err) {
            DBufFree(dbuf);
            return err;
        }
        if (!c) {
            break;
        }
        if (c == '<') {
            DynamicBuffer header;
            char const *val;
            DBufInit(&header);

            while(1) {
                c = ParseChar(p, &err, 0);
                if (err) {
                    DBufFree(&header);
                    return err;
                }
                if (!c || c == '>') {
                    break;
                }
                DBufPutc(&header, c);
            }
            if (!c) {
                Wprint(tr("Warning: Unterminated %%<...> substitution sequence"));
            }
            err = OK;
            val = FindTrigInfo(t, DBufValue(&header));
            DBufFree(&header);
            if (val) {
                SHIP_OUT(val);
            }
            continue;
        }
        if (c == '(') {
            DynamicBuffer orig;
            DynamicBuffer translated;
            DBufInit(&orig);
            DBufInit(&translated);
            while(1) {
                c = ParseChar(p, &err, 0);
                if (err) {
                    DBufFree(&orig);
                    return err;
                }
                if (!c || c == ')') {
                    break;
                }
                DBufPutc(&orig, c);
            }
            if (!c) {
                Wprint(tr("Warning: Unterminated %%(...) substitution sequence"));
            }
            err = OK;
            if (GetTranslatedStringTryingVariants(DBufValue(&orig), &translated)) {
                err = DBufPuts(dbuf, DBufValue(&translated));
            } else {
                err = DBufPuts(dbuf, DBufValue(&orig));
            }
            if (DebugFlag & DB_TRANSLATE) {
                TranslationTemplate(DBufValue(&orig));
            }
            DBufFree(&orig);
            DBufFree(&translated);
            if (err) return err;
            continue;
        }
        if (c == '*') {
            altmode = c;
            c = ParseChar(p, &err, 0);
            if (err) {
                DBufFree(dbuf);
                return err;
            }
            if (!c) {
                break;
            }
        }
        if (c == '{') {
            i = 0;
            ss = s + snprintf(s, sizeof(s), "subst_");
            while (1) {
                c = ParseChar(p, &err, 0);
                if (err) {
                    DBufFree(dbuf);
                    return err;
                }
                if (c == '}' || !c) {
                    break;
                }
                if (i < 64) {
                    *ss++ = tolower(c);
                    *ss = 0;
                    i++;
                }
            }
            if (!c) {
                Wprint(tr("Warning: Unterminated %%{...} substitution sequence"));
            }
            func = FindUserFunc(s);
            if (!func) {
                Wprint(tr("No substition function `%s' defined"), s);
                continue;
            }

            if (!check_subst_args(func, 3)) {
                continue;
            }
            snprintf(ss, sizeof(s) - (ss-s), "(%d,'%04d-%02d-%02d',%02d:%02d)",
                     altmode ? 1 : 0, y, m+1, d, h, min);
            expr = (char const *) s;
            r = EvalExprRunDisabled(&expr, &v, NULL);
            if (r == OK) {
                if (!DoCoerce(STR_TYPE, &v)) {
                    if (DBufPuts(dbuf, v.v.str) != OK) {
                        DestroyValue(v);
                        return E_NO_MEM;
                    }
                }
                DestroyValue(v);
            }
            continue;
        }
        done = 0;
        substname = get_function_override(c, 0);
        if (substname) {
            func = FindUserFunc(substname);
        } else {
            func = NULL;
        }
        if (func && check_subst_args(func, 3)) {
            snprintf(s, sizeof(s), "%s(%d,'%04d-%02d-%02d',%02d:%02d)",
                     substname, altmode ? 1 : 0, y, m+1, d, h, min);
            expr = (char const *) s;
            r = EvalExprRunDisabled(&expr, &v, NULL);
            if (r == OK) {
                if (v.type != INT_TYPE || v.v.val != 0) {
                    if (!DoCoerce(STR_TYPE, &v)) {
                        if (DBufPuts(dbuf, v.v.str) != OK) {
                            DestroyValue(v);
                            return E_NO_MEM;
                        }
                    }
                    DestroyValue(v);
                    continue;
                }
                DestroyValue(v);
            } else {
                Eprint("%s", GetErr(r));
            }
        }

        if (abs(diff) <= 1) {
            switch(UPPER(c)) {
            case 'A':
            case 'B':
            case 'C':
            case 'E':
            case 'F':
            case 'G':
            case 'H':
            case 'I':
            case 'J':
            case 'K':
            case 'L':
            case 'U':
            case 'V':
                snprintf(s, sizeof(s), "%s", (diff == 1 ? tr("tomorrow") :
                                              diff == -1 ? tr("yesterday") :
                                              tr("today")));
                SHIP_OUT(s);
                done = 1;
                break;

            default: done = 0;
            }
        }


        if (!done) {
            substname = get_function_override(c, 1);
            if (substname) {
                func = FindUserFunc(substname);
            } else {
                func = NULL;
            }
            if (func && check_subst_args(func, 3)) {
                snprintf(s, sizeof(s), "%s(%d,'%04d-%02d-%02d',%02d:%02d)",
                         substname, altmode ? 1 : 0, y, m+1, d, h, min);
                expr = (char const *) s;
                r = EvalExprRunDisabled(&expr, &v, NULL);
                if (r == OK) {
                    if (v.type != INT_TYPE || v.v.val != 0) {
                        if (!DoCoerce(STR_TYPE, &v)) {
                            if (DBufPuts(dbuf, v.v.str) != OK) {
                                DestroyValue(v);
                                return E_NO_MEM;
                            }
                        }
                        DestroyValue(v);
                        continue;
                    }
                    DestroyValue(v);
                } else {
                    Eprint("%s", GetErr(r));
                }
            }
            if (origtime == NO_TIME) {
                if ((c >= '0' && c <= '9')) {
                    Wprint(tr("`%%%c' substitution sequence should not be used without an AT clause"), c);
                }
            }

            switch(UPPER(c)) {
            case 'A':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s, %d %s, %d", get_day_name(dse%7), d,
                             get_month_name(m), y);
                } else {
                    snprintf(s, sizeof(s), "%s %s, %d %s, %d", tr("on"), get_day_name(dse%7), d,
                             get_month_name(m), y);
                }
                SHIP_OUT(s);
                break;

            case 'B':
                if (diff > 0) {
                    snprintf(s, sizeof(s), "in %d days' time", diff);
                } else {
                    snprintf(s, sizeof(s), "%d days ago", -diff);
                }
                SHIP_OUT(s);
                break;

            case 'C':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s", get_day_name(dse%7));
                } else {
                    snprintf(s, sizeof(s), "%s %s", tr("on"), get_day_name(dse%7));
                }
                SHIP_OUT(s);
                break;

            case 'D':
                snprintf(s, sizeof(s), "%d", d);
                SHIP_OUT(s);
                break;

            case 'E':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%02d%c%02d%c%04d", d, DateSep,
                             m+1, DateSep, y);
                } else {
                    snprintf(s, sizeof(s), "%s %02d%c%02d%c%04d", tr("on"), d, DateSep,
                             m+1, DateSep, y);
                }
                SHIP_OUT(s);
                break;

            case 'F':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%02d%c%02d%c%04d", m+1, DateSep, d, DateSep, y);
                } else {
                    snprintf(s, sizeof(s), "%s %02d%c%02d%c%04d", tr("on"), m+1, DateSep, d, DateSep, y);
                }
                SHIP_OUT(s);
                break;

            case 'G':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s, %d %s", get_day_name(dse%7), d, get_month_name(m));
                } else {
                    snprintf(s, sizeof(s), "%s %s, %d %s", tr("on"), get_day_name(dse%7), d, get_month_name(m));
                }
                SHIP_OUT(s);
                break;

            case 'H':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%02d%c%02d", d, DateSep, m+1);
                } else {
                    snprintf(s, sizeof(s), "%s %02d%c%02d", tr("on"), d, DateSep, m+1);
                }
                SHIP_OUT(s);
                break;

            case 'I':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%02d%c%02d", m+1, DateSep, d);
                } else {
                    snprintf(s, sizeof(s), "%s %02d%c%02d", tr("on"), m+1, DateSep, d);
                }
                SHIP_OUT(s);
                break;

            case 'J':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s, %s %d%s, %d", get_day_name(dse%7),
                             get_month_name(m), d, plu, y);
                } else {
                    snprintf(s, sizeof(s), "%s %s, %s %d%s, %d", tr("on"), get_day_name(dse%7),
                             get_month_name(m), d, plu, y);
                }
                SHIP_OUT(s);
                break;

            case 'K':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s, %s %d%s", get_day_name(dse%7),
                             get_month_name(m), d, plu);
                } else {
                    snprintf(s, sizeof(s), "%s %s, %s %d%s", tr("on"), get_day_name(dse%7),
                             get_month_name(m), d, plu);
                }
                SHIP_OUT(s);
                break;

            case 'L':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%04d%c%02d%c%02d", y, DateSep, m+1, DateSep, d);
                } else {
                    snprintf(s, sizeof(s), "%s %04d%c%02d%c%02d", tr("on"), y, DateSep, m+1, DateSep, d);
                }
                SHIP_OUT(s);
                break;

            case 'M':
                snprintf(s, sizeof(s), "%s", get_month_name(m));
                SHIP_OUT(s);
                break;

            case 'N':
                snprintf(s, sizeof(s), "%d", m+1);
                SHIP_OUT(s);
                break;

            case 'O':
                if (RealToday == DSEToday) snprintf(s, sizeof(s), " (%s)", tr("today"));
                else *s = 0;
                SHIP_OUT(s);
                break;

            case 'P':
                snprintf(s, sizeof(s), "%s", (diff == 1 ? "" : "s"));
                SHIP_OUT(s);
                break;

            case 'Q':
                snprintf(s, sizeof(s), "%s", (diff == 1 ? "'s" : "s'"));
                SHIP_OUT(s);
                break;

            case 'R':
                snprintf(s, sizeof(s), "%02d", d);
                SHIP_OUT(s);
                break;

            case 'S':
                snprintf(s, sizeof(s), "%s", plu);
                SHIP_OUT(s);
                break;

            case 'T':
                snprintf(s, sizeof(s), "%02d", m+1);
                SHIP_OUT(s);
                break;

            case 'U':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s, %d%s %s, %d", get_day_name(dse%7), d,
                             plu, get_month_name(m), y);
                } else {
                    snprintf(s, sizeof(s), "%s %s, %d%s %s, %d", tr("on"), get_day_name(dse%7), d,
                             plu, get_month_name(m), y);
                }
                SHIP_OUT(s);
                break;

            case 'V':
                if (altmode == '*' || !strcmp(tr("on"), "")) {
                    snprintf(s, sizeof(s), "%s, %d%s %s", get_day_name(dse%7), d, plu,
                             get_month_name(m));
                } else {
                    snprintf(s, sizeof(s), "%s %s, %d%s %s", tr("on"), get_day_name(dse%7), d, plu,
                             get_month_name(m));
                }
                SHIP_OUT(s);
                break;

            case 'W':
                snprintf(s, sizeof(s), "%s", get_day_name(dse%7));
                SHIP_OUT(s);
                break;

            case 'X':
                snprintf(s, sizeof(s), "%d", diff);
                SHIP_OUT(s);
                break;

            case 'Y':
                snprintf(s, sizeof(s), "%d", y);
                SHIP_OUT(s);
                break;

            case 'Z':
                snprintf(s, sizeof(s), "%d", y % 100);
                SHIP_OUT(s);
                break;

            case ':':
                if (t->is_todo && t->complete_through != NO_DATE && t->complete_through >= dse) {
                    snprintf(s, sizeof(s), " (%s)", tr("done"));
                    SHIP_OUT(s);
                }
                break;
            case '1':
                if (tdiff == 0)
                    snprintf(s, sizeof(s), "%s", tr("now"));
                else if (hdiff == 0)
                    snprintf(s, sizeof(s), "%d %s%s %s", mdiff, tr("minute"), mplu, when);
                else if (mdiff == 0)
                    snprintf(s, sizeof(s), "%d %s%s %s", hdiff, tr("hour"), hplu, when);
                else
                    snprintf(s, sizeof(s), "%d %s%s %s %d %s%s %s", hdiff, tr("hour"), hplu,
                             tr("and"), mdiff,
                             tr("minute"), mplu, when);
                SHIP_OUT(s);
                break;

            case '2':
                if (altmode == '*') {
                    snprintf(s, sizeof(s), "%d%c%02d%s", hh, TimeSep, min, pm);
                } else {
                    snprintf(s, sizeof(s), "%s %d%c%02d%s", tr("at"), hh, TimeSep, min, pm);
                }
                SHIP_OUT(s);
                break;

            case '3':
                if (altmode == '*') {
                    snprintf(s, sizeof(s), "%02d%c%02d", h, TimeSep, min);
                } else {
                    snprintf(s, sizeof(s), "%s %02d%c%02d", tr("at"), h, TimeSep, min);
                }
                SHIP_OUT(s);
                break;

            case '4':
                snprintf(s, sizeof(s), "%d", tdiff);
                SHIP_OUT(s);
                break;

            case '5':
                snprintf(s, sizeof(s), "%d", adiff);
                SHIP_OUT(s);
                break;

            case '6':
                snprintf(s, sizeof(s), "%s", when);
                SHIP_OUT(s);
                break;

            case '7':
                snprintf(s, sizeof(s), "%d", hdiff);
                SHIP_OUT(s);
                break;

            case '8':
                snprintf(s, sizeof(s), "%d", mdiff);
                SHIP_OUT(s);
                break;

            case '9':
                snprintf(s, sizeof(s), "%s", mplu);
                SHIP_OUT(s);
                break;

            case '0':
                snprintf(s, sizeof(s), "%s", hplu);
                SHIP_OUT(s);
                break;

            case '!':
            case '?':
                if (c == '!') {
                    is = tr("is");
                    was = tr("was");
                } else {
                    is = tr("are");
                    was = tr("were");
                }
                if (altmode) {
                    bangdiff = rdiff;
                } else {
                    bangdiff = diff;
                }
                if (bangdiff > 0) {
                    snprintf(s, sizeof(s), "%s", is);
                } else if (bangdiff < 0) {
                    snprintf(s, sizeof(s), "%s", was);
                } else {
                    snprintf(s, sizeof(s), "%s", (tdiff >= 0 ? is : was));
                }
                SHIP_OUT(s);
                break;

            case '@':
                snprintf(s, sizeof(s), "%d%c%02d%s", chh, TimeSep, cmin, cpm);
                SHIP_OUT(s);
                break;

            case '#':
                snprintf(s, sizeof(s), "%02d%c%02d", ch, TimeSep, cmin);
                SHIP_OUT(s);
                break;

            case '_':
                if (PsCal == PSCAL_LEVEL2 || PsCal == PSCAL_LEVEL3 || DoCalendar || (mode != CAL_MODE && mode != ADVANCE_MODE && !(MsgCommand && *MsgCommand))) {
                    snprintf(s, sizeof(s), "%s", NL);
                } else {
                    snprintf(s, sizeof(s), " ");
                }
                SHIP_OUT(s);
                break;

            case QUOTE_MARKER:
                /* Swallow any QUOTE_MARKERs which may somehow creep in... */
                break;

            case '"':
                if (DontSuppressQuoteMarkers) {
                    if (DBufPutc(dbuf, '%') != OK) return E_NO_MEM;
                    if (DBufPutc(dbuf, c) != OK) return E_NO_MEM;
                } else {
                    if (DBufPutc(dbuf, QUOTE_MARKER) != OK) return E_NO_MEM;
                    has_quote = 1;
                }
                break;

            default:
                if (DBufPutc(dbuf, c) != OK) return E_NO_MEM;
            }
        }

        if (isupper(c)) {
            os = DBufValue(dbuf);
            os += strlen(os) - strlen(s);
            if (os >= DBufValue(dbuf)) {
                *os = UPPER(*os);
            }
        }
    }

/* We're outside the big while loop.  The only way to get here is for c to
   be null.  Now we go through and delete %" sequences, if it's the
   NORMAL_MODE, or retain only things within a %" sequence if it's the
   CAL_MODE. */

/* If there are NO quotes, then:  If CAL_MODE && RUN_TYPE, we don't want the
   reminder in the calendar.  Zero the output buffer and quit. */
    if (!has_quote) {
        if ((mode == ADVANCE_MODE || mode == CAL_MODE) && t->typ == RUN_TYPE) {
            *DBufValue(dbuf) = 0;
            dbuf->len = 0;
        }
        return OK;
    }

/* There ARE quotes.  If in CAL_MODE, delete everything before first quote
   and after second quote.  If in NORMAL_MODE, delete the %" sequences. */

    ss = DBufValue(dbuf) + origLen;
    os = ss;
    if (mode == NORMAL_MODE || mode == ADVANCE_MODE) {
        while (*ss) {
            if (*ss != QUOTE_MARKER) *os++ = *ss;
            ss++;
        }
        *os = 0;
    } else {
/* Skip past the quote marker */
        while (*ss && (*ss != QUOTE_MARKER)) ss++;

/* Security check... actually, *s must == QUOTE_MARKER at this point, but
   it doesn't hurt to make it a bit robust. */
        if (*ss) ss++;

/* Copy the output until the next QUOTE_MARKER */
        while (*ss && (*ss != QUOTE_MARKER)) *os++ = *ss++;
        *os = 0;
    }

    /* Violating encapsulation here!!!! */
    dbuf->len = strlen(dbuf->buffer);

    return OK;
}


/***************************************************************/
/*                                                             */
/*  DoSubstFromString                                          */
/*                                                             */
/*  DoSubst consumes input from a parser.  This function       */
/*  consumes characters from a string.  It also provides       */
/*  default triggers and a mode of NORMAL_MODE.                */
/*                                                             */
/***************************************************************/
int DoSubstFromString(char const *source, DynamicBuffer *dbuf,
                      int dse, int tim)
{
    Trigger tempTrig;
    TimeTrig tempTime;
    Parser tempP;
    int r;

    if (dse == NO_DATE) dse=DSEToday;
    if (tim == NO_TIME) tim=MinutesPastMidnight(0);
    CreateParser(source, &tempP);
    tempP.allownested = 0;
    tempTrig.infos = NULL;
    DBufInit(&tempTrig.tags);
    tempTrig.typ = MSG_TYPE;
    tempTime.ttime = tim;

    r = DoSubst(&tempP, dbuf, &tempTrig, &tempTime, dse, NORMAL_MODE);
    DestroyParser(&tempP);
    return r;
}
