/***************************************************************/
/*                                                             */
/*  FUNCS.C                                                    */
/*                                                             */
/*  This file contains the built-in functions used in          */
/*  expressions.                                               */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/
#include "version.h"
#include "config.h"

#ifdef REM_USE_WCHAR
#define _XOPEN_SOURCE 600
#include <wctype.h>
#include <wchar.h>
#endif

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <ctype.h>
#include <math.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>

#include <sys/stat.h>

#ifdef TM_IN_SYS_TIME
#include <sys/time.h>
#else
#include <time.h>
#endif

#ifndef R_OK
#define R_OK 4
#define W_OK 2
#define X_OK 1
#endif

#include "types.h"
#include "globals.h"
#include "protos.h"
#include "err.h"

/* Defines that used to be static variables */
#define Nargs (info->nargs)
#define RetVal (info->retval)

#define DBG(x) do { if (DebugFlag & DB_PRTEXPR) { x; } } while(0)
/* Debugging helpers for "choose()", "iif(), etc. */
#define PUT(x) DBufPuts(&DebugBuf, x)
#define OUT() do { fprintf(ErrFp, "%s\n", DBufValue(&DebugBuf)); DBufFree(&DebugBuf); } while(0)

/* Last error from catch() */
static int LastCatchError = OK;

static int
solstice_equinox_for_year(int y, int which);

/* Function prototypes */
static int F_              (func_info *);
static int FADawn          (func_info *);
static int FADusk          (func_info *);
static int FAbs            (func_info *);
static int FAccess         (func_info *);
static int FAmpm           (func_info *);
static int FAnsicolor      (func_info *);
static int FArgs           (func_info *);
static int FAsc            (func_info *);
static int FBaseyr         (func_info *);
static int FCatch          (expr_node *, Value *, Value *, int *);
static int FCatchErr       (func_info *);
static int FChar           (func_info *);
static int FChoose         (expr_node *, Value *, Value *, int *);
static int FCoerce         (func_info *);
static int FColumns        (func_info *);
static int FCurrent        (func_info *);
static int FDate           (func_info *);
static int FDateTime       (func_info *);
static int FDatepart       (func_info *);
static int FDawn           (func_info *);
static int FDay            (func_info *);
static int FDaysinmon      (func_info *);
static int FDefined        (func_info *);
static int FDosubst        (func_info *);
static int FDusk           (func_info *);
static int FEasterdate     (func_info *);
static int FEscape         (func_info *);
static int FEval           (func_info *);
static int FEvalTrig       (func_info *);
static int FFiledate       (func_info *);
static int FFiledatetime   (func_info *);
static int FFiledir        (func_info *);
static int FFilename       (func_info *);
static int FGetenv         (func_info *);
static int FHebdate        (func_info *);
static int FHebday         (func_info *);
static int FHebmon         (func_info *);
static int FHebyear        (func_info *);
static int FHour           (func_info *);
static int FHtmlEscape     (func_info *);
static int FHtmlStriptags  (func_info *);
static int FIif            (expr_node *, Value *, Value *, int *);
static int FIndex          (func_info *);
static int FIsAny          (expr_node *, Value *, Value *, int *);
static int FIsconst        (expr_node *, Value *, Value *, int *);
static int FIsdst          (func_info *);
static int FIsleap         (func_info *);
static int FIsomitted      (func_info *);
static int FLanguage       (func_info *);
static int FLocalToUTC     (func_info *);
static int FLower          (func_info *);
static int FMax            (func_info *);
static int FMin            (func_info *);
static int FMinsfromutc    (func_info *);
static int FMinute         (func_info *);
static int FMon            (func_info *);
static int FMonnum         (func_info *);
static int FMoondate       (func_info *);
static int FMoondatetime   (func_info *);
static int FMoonphase      (func_info *);
static int FMoonrise       (func_info *);
static int FMoonrisedir    (func_info *);
static int FMoonset        (func_info *);
static int FMoonsetdir     (func_info *);
static int FMoontime       (func_info *);
static int FMultiTrig      (func_info *);
static int FNDawn          (func_info *);
static int FNDusk          (func_info *);
static int FNonconst       (func_info *);
static int FNonomitted     (func_info *);
static int FNow            (func_info *);
static int FOrd            (func_info *);
static int FOrthodoxeaster (func_info *);
static int FOstype         (func_info *);
static int FPad            (func_info *);
static int FPlural         (func_info *);
static int FPsmoon         (func_info *);
static int FPsshade        (func_info *);
static int FRealCurrent    (func_info *);
static int FRealnow        (func_info *);
static int FRealtoday      (func_info *);
static int FRows           (func_info *);
static int FSgn            (func_info *);
static int FShell          (func_info *);
static int FShellescape    (func_info *);
static int FSlide          (func_info *);
static int FSoleq          (func_info *);
static int FStdout         (func_info *);
static int FStrlen         (func_info *);
static int FSubstr         (func_info *);
static int FSunrise        (func_info *);
static int FSunset         (func_info *);
static int FTime           (func_info *);
static int FTimepart       (func_info *);
static int FTimezone       (func_info *);
static int FToday          (func_info *);
static int FTrig           (func_info *);
static int FTrigback       (func_info *);
static int FTrigbase       (func_info *info);
static int FTrigdate       (func_info *);
static int FTrigdatetime   (func_info *);
static int FTrigdelta      (func_info *);
static int FTrigduration   (func_info *);
static int FTriginfo       (func_info *);
static int FTrigeventduration(func_info *);
static int FTrigeventstart (func_info *);
static int FTrigfrom       (func_info *);
static int FTrigger        (func_info *);
static int FTrigpriority   (func_info *);
static int FTrigrep        (func_info *);
static int FTrigscanfrom   (func_info *);
static int FTrigtags       (func_info *);
static int FTrigtime       (func_info *);
static int FTrigtimedelta  (func_info *);
static int FTrigtimerep    (func_info *);
static int FTriguntil      (func_info *);
static int FTrigvalid      (func_info *);
static int FTypeof         (func_info *);
static int FTzconvert      (func_info *);
static int FUTCToLocal     (func_info *);
static int FUpper          (func_info *);
static int FValue          (func_info *);
static int FVersion        (func_info *);
static int FWeekno         (func_info *);
static int FWkday          (func_info *);
static int FWkdaynum       (func_info *);
static int FYear           (func_info *);

static int SunStuff        (int rise, double cosz, int dse);
static int tz_set_tz       (char const *tz);

/* Caches for extracting months, days, years from dates - may
   improve performance slightly. */
static int CacheDse = -1;
static int CacheYear, CacheMon, CacheDay;

static int CacheHebDse = -1;
static int CacheHebYear, CacheHebMon, CacheHebDay;

/* Macro for accessing arguments from the value stack - args are numbered
   from 0 to (Nargs - 1) */
#define ARG(x) (info->args[x])

#define ARGV(x) ARG(x).v.val
#define ARGSTR(x) ARG(x).v.str

#define ASSERT_TYPE(x, t) if (ARG(x).type != t) return E_BAD_TYPE

/* Macro for getting date part of a date or datetime value */
#define DATEPART(x) ((x).type == DATE_TYPE ? (x).v.val : ((x).v.val / MINUTES_PER_DAY))

/* Macro for getting time part of a time or datetime value */
#define TIMEPART(x) ((x).type == TIME_TYPE ? (x).v.val : ((x).v.val % MINUTES_PER_DAY))

#define HASDATE(x) ((x).type & DATE_TYPE)
#define HASTIME(x) ((x).type & TIME_TYPE)

/* Macro for copying a value while destroying original copy */
#define DCOPYVAL(x, y) ( (x) = (y), (y).type = ERR_TYPE )

/* Get at RetVal.v.val easily */
#define RETVAL info->retval.v.val

/* Convenience macros */
#define UPPER(c) (islower(c) ? toupper(c) : c)
#define LOWER(c) (isupper(c) ? tolower(c) : c)

/* The array holding the built-in functions. */
BuiltinFunc Func[] = {
/*      Name            minargs maxargs is_constant func             newfunc*/
    {   "_",            1,      1,      0,          F_, NULL },
    {   "abs",          1,      1,      1,          FAbs, NULL },
    {   "access",       2,      2,      0,          FAccess, NULL },
    {   "adawn",        0,      1,      0,          FADawn, NULL},
    {   "adusk",        0,      1,      0,          FADusk, NULL},
    {   "ampm",         1,      4,      1,          FAmpm, NULL },
    {   "ansicolor",    1,      5,      1,          FAnsicolor, NULL },
    {   "args",         1,      1,      0,          FArgs, NULL },
    {   "asc",          1,      1,      1,          FAsc, NULL },
    {   "baseyr",       0,      0,      1,          FBaseyr, NULL },
    {   "catch",        2,      2,      1,          NULL, FCatch }, /* NEW-STYLE */
    {   "catcherr",     0,      0,      0,          FCatchErr, NULL },
    {   "char",         1,      NO_MAX, 1,          FChar, NULL },
    {   "choose",       2,      NO_MAX, 1,          NULL, FChoose }, /*NEW-STYLE*/
    {   "coerce",       2,      2,      1,          FCoerce, NULL },
    {   "columns",      0,      1,      0,          FColumns, NULL },
    {   "const",        1,      1,      1,          FNonconst, NULL },
    {   "current",      0,      0,      0,          FCurrent, NULL },
    {   "date",         3,      3,      1,          FDate, NULL },
    {   "datepart",     1,      1,      1,          FDatepart, NULL },
    {   "datetime",     2,      5,      1,          FDateTime, NULL },
    {   "dawn",         0,      1,      0,          FDawn, NULL },
    {   "day",          1,      1,      1,          FDay, NULL },
    {   "daysinmon",    2,      2,      1,          FDaysinmon, NULL },
    {   "defined",      1,      1,      0,          FDefined, NULL },
    {   "dosubst",      1,      3,      0,          FDosubst, NULL },
    {   "dusk",         0,      1,      0,          FDusk, NULL },
    {   "easterdate",   0,      1,      0,          FEasterdate, NULL },
    {   "escape",       1,      2,      1,          FEscape, NULL },
    {   "eval",         1,      1,      1,          FEval, NULL },
    {   "evaltrig",     1,      2,      0,          FEvalTrig, NULL },
    {   "filedate",     1,      1,      0,          FFiledate, NULL },
    {   "filedatetime", 1,      1,      0,          FFiledatetime, NULL },
    {   "filedir",      0,      0,      0,          FFiledir, NULL },
    {   "filename",     0,      0,      0,          FFilename, NULL },
    {   "getenv",       1,      1,      0,          FGetenv, NULL },
    {   "hebdate",      2,      5,      0,          FHebdate, NULL },
    {   "hebday",       1,      1,      0,          FHebday, NULL },
    {   "hebmon",       1,      1,      0,          FHebmon, NULL },
    {   "hebyear",      1,      1,      0,          FHebyear, NULL },
    {   "hour",         1,      1,      1,          FHour, NULL },
    {   "htmlescape",   1,      1,      1,          FHtmlEscape, NULL },
    {   "htmlstriptags",1,      1,      1,          FHtmlStriptags, NULL },
    {   "iif",          1,      NO_MAX, 1,          NULL, FIif }, /*NEW-STYLE*/
    {   "index",        2,      3,      1,          FIndex, NULL },
    {   "isany",        1,      NO_MAX, 1,          NULL, FIsAny }, /*NEW-STYLE*/
    {   "isconst",      1,      1,      1,          NULL, FIsconst }, /*NEW-STYLE*/
    {   "isdst",        0,      2,      0,          FIsdst, NULL },
    {   "isleap",       1,      1,      1,          FIsleap, NULL },
    {   "isomitted",    1,      1,      0,          FIsomitted, NULL },
    {   "language",     0,      0,      1,          FLanguage, NULL },
    {   "localtoutc",   1,      1,      1,          FLocalToUTC, NULL },
    {   "lower",        1,      1,      1,          FLower, NULL },
    {   "max",          1,      NO_MAX, 1,          FMax, NULL },
    {   "min",          1,      NO_MAX, 1,          FMin, NULL },
    {   "minsfromutc",  0,      2,      0,          FMinsfromutc, NULL },
    {   "minute",       1,      1,      1,          FMinute, NULL },
    {   "mon",          1,      1,      1,          FMon, NULL },
    {   "monnum",       1,      1,      1,          FMonnum, NULL },
    {   "moondate",     1,      3,      0,          FMoondate, NULL },
    {   "moondatetime", 1,      3,      0,          FMoondatetime, NULL },
    {   "moonphase",    0,      2,      0,          FMoonphase, NULL },
    {   "moonrise",     0,      1,      0,          FMoonrise, NULL },
    {   "moonrisedir",  0,      1,      0,          FMoonrisedir, NULL },
    {   "moonset",      0,      1,      0,          FMoonset, NULL },
    {   "moonsetdir",   0,      1,      0,          FMoonsetdir, NULL },
    {   "moontime",     1,      3,      0,          FMoontime, NULL },
    {   "multitrig",    1,      NO_MAX, 0,          FMultiTrig, NULL },
    {   "ndawn",        0,      1,      0,          FNDawn, NULL },
    {   "ndusk",        0,      1,      0,          FNDusk, NULL },
    {   "nonconst",     1,      1,      0,          FNonconst, NULL },
    {   "nonomitted",   2,      NO_MAX, 0,          FNonomitted, NULL },
    {   "now",          0,      0,      0,          FNow, NULL },
    {   "ord",          1,      1,      1,          FOrd, NULL },
    {   "orthodoxeaster",0,     1,      0,          FOrthodoxeaster, NULL },
    {   "ostype",       0,      0,      1,          FOstype, NULL },
    {   "pad",          3,      4,      1,          FPad, NULL },
    {   "plural",       1,      3,      1,          FPlural, NULL },
    {   "psmoon",       1,      4,      1,          FPsmoon, NULL },
    {   "psshade",      1,      3,      1,          FPsshade, NULL },
    {   "realcurrent",  0,      0,      0,          FRealCurrent, NULL },
    {   "realnow",      0,      0,      0,          FRealnow, NULL },
    {   "realtoday",    0,      0,      0,          FRealtoday, NULL },
    {   "rows",         0,      0,      0,          FRows, NULL },
    {   "sgn",          1,      1,      1,          FSgn, NULL },
    {   "shell",        1,      2,      0,          FShell, NULL },
    {   "shellescape",  1,      1,      1,          FShellescape, NULL },
    {   "slide",        2,      NO_MAX, 0,          FSlide, NULL },
    {   "soleq",        1,      2,      0,          FSoleq, NULL },
    {   "stdout",       0,      0,      0,          FStdout, NULL },
    {   "strlen",       1,      1,      1,          FStrlen, NULL },
    {   "substr",       2,      3,      1,          FSubstr, NULL },
    {   "sunrise",      0,      1,      0,          FSunrise, NULL },
    {   "sunset",       0,      1,      0,          FSunset, NULL },
    {   "time",         2,      2,      1,          FTime, NULL },
    {   "timepart",     1,      1,      1,          FTimepart, NULL },
    {   "timezone",     0,      1,      0,          FTimezone, NULL },
    {   "today",        0,      0,      0,          FToday, NULL },
    {   "trig",         0,      NO_MAX, 0,          FTrig, NULL },
    {   "trigback",     0,      0,      0,          FTrigback, NULL },
    {   "trigbase",     0,      0,      0,          FTrigbase, NULL },
    {   "trigdate",     0,      0,      0,          FTrigdate, NULL },
    {   "trigdatetime", 0,      0,      0,          FTrigdatetime, NULL },
    {   "trigdelta",    0,      0,      0,          FTrigdelta, NULL },
    {   "trigduration", 0,      0,      0,          FTrigduration, NULL },
    {   "trigeventduration", 0, 0,      0,          FTrigeventduration, NULL },
    {   "trigeventstart", 0,    0,      0,          FTrigeventstart, NULL },
    {   "trigfrom",     0,      0,      0,          FTrigfrom, NULL },
    {   "trigger",      1,      3,      0,          FTrigger, NULL },
    {   "triginfo",     1,      1,      0,          FTriginfo, NULL },
    {   "trigpriority", 0,      0,      0,          FTrigpriority, NULL },
    {   "trigrep",      0,      0,      0,          FTrigrep, NULL },
    {   "trigscanfrom", 0,      0,      0,          FTrigscanfrom, NULL },
    {   "trigtags",     0,      0,      0,          FTrigtags, NULL },
    {   "trigtime",     0,      0,      0,          FTrigtime, NULL },
    {   "trigtimedelta",0,      0,      0,          FTrigtimedelta, NULL },
    {   "trigtimerep",  0,      0,      0,          FTrigtimerep, NULL },
    {   "triguntil",    0,      0,      0,          FTriguntil, NULL },
    {   "trigvalid",    0,      0,      0,          FTrigvalid, NULL },
    {   "typeof",       1,      1,      1,          FTypeof, NULL },
    {   "tzconvert",    2,      3,      0,          FTzconvert, NULL },
    {   "upper",        1,      1,      1,          FUpper, NULL },
    {   "utctolocal",   1,      1,      1,          FUTCToLocal, NULL },
    {   "value",        1,      2,      0,          FValue, NULL },
    {   "version",      0,      0,      1,          FVersion, NULL },
    {   "weekno",       0,      3,      0,          FWeekno, NULL },
    {   "wkday",        1,      1,      1,          FWkday, NULL },
    {   "wkdaynum",     1,      1,      1,          FWkdaynum, NULL },
    {   "year",         1,      1,      1,          FYear, NULL }
};

/* Need a variable here - Func[] array not really visible to outside. */
int NumFuncs = sizeof(Func) / sizeof(BuiltinFunc) ;

/***************************************************************/
/*                                                             */
/*  RetStrVal                                                  */
/*                                                             */
/*  Return a string value from a function.                     */
/*                                                             */
/***************************************************************/
static int RetStrVal(char const *s, func_info *info)
{
    RetVal.type = STR_TYPE;
    if (!s) {
        RetVal.v.str = malloc(1);
        if (RetVal.v.str) *RetVal.v.str = 0;
    } else {
        RetVal.v.str = StrDup(s);
    }

    if (!RetVal.v.str) {
        RetVal.type = ERR_TYPE;
        return E_NO_MEM;
    }
    return OK;
}


/***************************************************************/
/*                                                             */
/*  F_ - look up a string in the translation table             */
/*                                                             */
/***************************************************************/
static int F_(func_info *info)
{
    DynamicBuffer translated;
    int r;

    DBufInit(&translated);
    ASSERT_TYPE(0, STR_TYPE);
    r = GetTranslatedStringTryingVariants(ARGSTR(0), &translated);
    if (!r) {
        DCOPYVAL(RetVal, ARG(0));
        return OK;
    }
    r = RetStrVal(DBufValue(&translated), info);
    DBufFree(&translated);
    if (DebugFlag & DB_TRANSLATE) {
        TranslationTemplate(ARGSTR(0));
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  FStrlen - string length                                    */
/*                                                             */
/***************************************************************/
static int FStrlen(func_info *info)
{
    ASSERT_TYPE(0, STR_TYPE);
    RetVal.type = INT_TYPE;
    size_t l = strlen(ARGSTR(0));
    if (l > INT_MAX) return E_2HIGH;
    RETVAL = (int) l;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FBaseyr - system base year                                 */
/*                                                             */
/***************************************************************/
static int FBaseyr(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = BASE;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FDate - make a date from year, month, day.                 */
/*                                                             */
/***************************************************************/
static int FDate(func_info *info)
{
    int y, m, d;
    int ytemp, mtemp, dtemp;

    /* Any arg can be a date (in which case we use the corresponding
       component) or an integer */
    if (HASDATE(ARG(0))) {
        FromDSE(DATEPART(ARG(0)), &ytemp, &mtemp, &dtemp);
        y = ytemp;
    } else {
        ASSERT_TYPE(0, INT_TYPE);
        y = ARGV(0);
    }

    if (HASDATE(ARG(1))) {
        FromDSE(DATEPART(ARG(1)), &ytemp, &mtemp, &dtemp);
        m = mtemp;
    } else {
        ASSERT_TYPE(1, INT_TYPE);
        m = ARGV(1) - 1;
    }

    if (HASDATE(ARG(2))) {
        FromDSE(DATEPART(ARG(2)), &ytemp, &mtemp, &dtemp);
        d = dtemp;
    } else {
        ASSERT_TYPE(2, INT_TYPE);
        d = ARGV(2);
    }

    if (!DateOK(y, m, d)) {
        return E_BAD_DATE;
    }
    RetVal.type = DATE_TYPE;
    RETVAL = DSE(y, m, d);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FDateTime - make a datetime from one of these combos:      */
/*  DATE, TIME                                                 */
/*  DATE, HOUR, MINUTE                                         */
/*  YEAR, MONTH, DAY, TIME                                     */
/*  YEAR, MONTH, DAY, HOUR, MINUTE                             */
/*                                                             */
/***************************************************************/
static int FDateTime(func_info *info)
{
    int y, m, d;

    RetVal.type = DATETIME_TYPE;

    switch(Nargs) {
    case 2:
        if (ARG(0).type != DATE_TYPE ||
            ARG(1).type != TIME_TYPE) return E_BAD_TYPE;
        RETVAL = (MINUTES_PER_DAY * ARGV(0)) + ARGV(1);
        return OK;
    case 3:
        if (ARG(0).type != DATE_TYPE ||
            ARG(1).type != INT_TYPE ||
            ARG(2).type != INT_TYPE) return E_BAD_TYPE;
        if (ARGV(1) < 0 || ARGV(2) < 0) return E_2LOW;
        if (ARGV(1) > 23 || ARGV(2) > 59) return E_2HIGH;
        RETVAL = (MINUTES_PER_DAY * ARGV(0)) + 60 * ARGV(1) + ARGV(2);
        return OK;
    case 4:
        if (ARG(0).type != INT_TYPE ||
            ARG(1).type != INT_TYPE ||
            ARG(2).type != INT_TYPE ||
            ARG(3).type != TIME_TYPE) return E_BAD_TYPE;
        y = ARGV(0);
        m = ARGV(1) - 1;
        d = ARGV(2);

        if (!DateOK(y, m, d)) return E_BAD_DATE;
        RETVAL = DSE(y, m, d) * MINUTES_PER_DAY + ARGV(3);
        return OK;
    case 5:
        if (ARG(0).type != INT_TYPE ||
            ARG(1).type != INT_TYPE ||
            ARG(2).type != INT_TYPE ||
            ARG(3).type != INT_TYPE ||
            ARG(4).type != INT_TYPE) return E_BAD_TYPE;

        y = ARGV(0);
        m = ARGV(1) - 1;
        d = ARGV(2);
        if (!DateOK(y, m, d)) return E_BAD_DATE;

        if (ARGV(3) < 0 || ARGV(4) < 0) return E_2LOW;
        if (ARGV(3) > 23 || ARGV(4) > 59) return E_2HIGH;
        RETVAL = DSE(y, m, d) * MINUTES_PER_DAY + ARGV(3) * 60 + ARGV(4);
        return OK;

    default:
        return E_2MANY_ARGS;
    }
}

/***************************************************************/
/*                                                             */
/*  FCoerce - type coercion function.                          */
/*                                                             */
/***************************************************************/
static int FCoerce(func_info *info)
{
    char const *s;
    char const *v = PrintValue(&(ARG(1)), NULL);
    ASSERT_TYPE(0, STR_TYPE);
    s = ARGSTR(0);

    int r = OK;

    /* Copy the value of ARG(1) into RetVal, and make ARG(1) invalid so
       it won't be destroyed */
    DCOPYVAL(RetVal, ARG(1));

    if (! StrCmpi(s, "int")) r =  DoCoerce(INT_TYPE, &RetVal);
    else if (! StrCmpi(s, "date")) r =  DoCoerce(DATE_TYPE, &RetVal);
    else if (! StrCmpi(s, "time")) r =  DoCoerce(TIME_TYPE, &RetVal);
    else if (! StrCmpi(s, "string")) r =  DoCoerce(STR_TYPE, &RetVal);
    else if (! StrCmpi(s, "datetime")) r =  DoCoerce(DATETIME_TYPE, &RetVal);
    else {
        Eprint("coerce(): Invalid type `%s'", s);
        return E_CANT_COERCE;
    }
    if (r) {
        Eprint("coerce(): Cannot convert %s to %s", v, s);
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  FNonconst - return arg, but with nonconst_expr flag set    */
/*                                                             */
/***************************************************************/
static int FNonconst(func_info *info)
{
    DCOPYVAL(RetVal, ARG(0));
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FMax - select maximum from a list of args.                 */
/*                                                             */
/***************************************************************/
static int FMax(func_info *info)
{
    Value *maxptr;
    int i;
    char type;

    maxptr = &ARG(0);
    type = maxptr->type;

    for (i=1; i<Nargs; i++) {
        if (ARG(i).type != type) return E_BAD_TYPE;
        if (type != STR_TYPE) {
            if (ARG(i).v.val > maxptr->v.val) maxptr = &ARG(i);
        } else {
            if (strcmp(ARG(i).v.str, maxptr->v.str) > 0) maxptr = &ARG(i);
        }
    }
    DCOPYVAL(RetVal, *maxptr);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FMin - select minimum from a list of args.                 */
/*                                                             */
/***************************************************************/
static int FMin(func_info *info)
{
    Value *minptr;
    int i;
    char type;

    minptr = &ARG(0);
    type = minptr->type;

    for (i=1; i<Nargs; i++) {
        if (ARG(i).type != type) return E_BAD_TYPE;
        if (type != STR_TYPE) {
            if (ARG(i).v.val < minptr->v.val) minptr = &ARG(i);
        } else {
            if (strcmp(ARG(i).v.str, minptr->v.str) < 0) minptr = &ARG(i);
        }
    }
    DCOPYVAL(RetVal, *minptr);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FAsc - ASCII value of first char of string                 */
/*                                                             */
/***************************************************************/
static int FAsc(func_info *info)
{
    ASSERT_TYPE(0, STR_TYPE);
    RetVal.type = INT_TYPE;
    RETVAL = *(ARGSTR(0));
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FChar - build a string from ASCII values                   */
/*                                                             */
/***************************************************************/
static int FChar(func_info *info)
{

    int i, len;

/* Special case of one arg - if given ascii value 0, create empty string */
    if (Nargs == 1) {
        ASSERT_TYPE(0, INT_TYPE);
        if (ARGV(0) < -128) return E_2LOW;
        if (ARGV(0) > 255) return E_2HIGH;
        len = ARGV(0) ? 2 : 1;
        RetVal.v.str = malloc(len);
        if (!RetVal.v.str) return E_NO_MEM;
        RetVal.type = STR_TYPE;
        *(RetVal.v.str) = ARGV(0);
        if (len>1) *(RetVal.v.str + 1) = 0;
        return OK;
    }

    RetVal.v.str = malloc(Nargs + 1);
    if (!RetVal.v.str) return E_NO_MEM;
    RetVal.type = STR_TYPE;
    for (i=0; i<Nargs; i++) {
        if (ARG(i).type != INT_TYPE) {
            free(RetVal.v.str);
            RetVal.type = ERR_TYPE;
            return E_BAD_TYPE;
        }
        if (ARG(i).v.val < -128 || ARG(i).v.val == 0) {
            free(RetVal.v.str);
            RetVal.type = ERR_TYPE;
            return E_2LOW;
        }
        if (ARG(i).v.val > 255) {
            free(RetVal.v.str);
            RetVal.type = ERR_TYPE;
            return E_2HIGH;
        }
        *(RetVal.v.str + i) = ARG(i).v.val;
    }
    *(RetVal.v.str + Nargs) = 0;
    return OK;
}
/***************************************************************/
/*                                                             */
/*  Functions for extracting the components of a date.         */
/*                                                             */
/*  FDay - get day of month                                    */
/*  FMonnum - get month (1-12)                                 */
/*  FYear - get year                                           */
/*  FWkdaynum - get weekday num (0 = Sun)                      */
/*  FWkday - get weekday (string)                              */
/*  FMon - get month (string)                                  */
/*                                                             */
/***************************************************************/
static int FDay(func_info *info)
{
    int y, m, d, v;
    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));

    if (v == CacheDse)
        d = CacheDay;
    else {
        FromDSE(v, &y, &m, &d);
        CacheDse = v;
        CacheYear = y;
        CacheMon = m;
        CacheDay = d;
    }
    RetVal.type = INT_TYPE;
    RETVAL = d;
    return OK;
}

static int FMonnum(func_info *info)
{
    int y, m, d, v;
    Token tok;
    if (ARG(0).type == STR_TYPE) {
        /* Convert a month name to a month number */
        FindToken(ARG(0).v.str, &tok);
        if (tok.type != T_Month) {
            return E_BAD_TYPE;
        }
        RetVal.type = INT_TYPE;
        RETVAL = tok.val + 1;
        return OK;
    }

    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));

    if (v == CacheDse)
        m = CacheMon;
    else {
        FromDSE(v, &y, &m, &d);
        CacheDse = v;
        CacheYear = y;
        CacheMon = m;
        CacheDay = d;
    }
    RetVal.type = INT_TYPE;
    RETVAL = m+1;
    return OK;
}

static int FYear(func_info *info)
{
    int y, m, d, v;
    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));

    if (v == CacheDse)
        y = CacheYear;
    else {
        FromDSE(v, &y, &m, &d);
        CacheDse = v;
        CacheYear = y;
        CacheMon = m;
        CacheDay = d;
    }
    RetVal.type = INT_TYPE;
    RETVAL = y;
    return OK;
}

static int FWkdaynum(func_info *info)
{
    int v;
    Token tok;
    if (ARG(0).type == STR_TYPE) {
        /* Convert a day name to a day number */
        FindToken(ARG(0).v.str, &tok);
        if (tok.type != T_WkDay) {
            return E_BAD_TYPE;
        }
        RetVal.type = INT_TYPE;
        RETVAL = (tok.val + 1) % 7;
        return OK;
    }
    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));

    RetVal.type = INT_TYPE;

    /* Correct so that 0 = Sunday */
    RETVAL = (v+1) % 7;
    return OK;
}

static int FWkday(func_info *info)
{
    char const *s;

    if (!HASDATE(ARG(0)) && ARG(0).type != INT_TYPE) return E_BAD_TYPE;
    if (ARG(0).type == INT_TYPE) {
        if (ARGV(0) < 0) return E_2LOW;
        if (ARGV(0) > 6) return E_2HIGH;
        /* Convert 0=Sun to 0=Mon */
        ARGV(0)--;
        if (ARGV(0) < 0) ARGV(0) = 6;
        s = get_day_name(ARGV(0));
    } else s = get_day_name(DATEPART(ARG(0)) % 7);
    return RetStrVal(s, info);
}

static int FMon(func_info *info)
{
    char const *s;
    int y, m, d, v;

    if (!HASDATE(ARG(0)) && ARG(0).type != INT_TYPE) return E_BAD_TYPE;

    if (ARG(0).type == INT_TYPE) {
        m = ARGV(0) - 1;
        if (m < 0) return E_2LOW;
        if (m > 11) return E_2HIGH;
    } else {
        v = DATEPART(ARG(0));
        if (v == CacheDse)
            m = CacheMon;
        else {
            FromDSE(v, &y, &m, &d);
            CacheDse = v;
            CacheYear = y;
            CacheMon = m;
            CacheDay = d;
        }
    }
    s = get_month_name(m);
    return RetStrVal(s, info);
}

/***************************************************************/
/*                                                             */
/*  FHour - extract hour from a time                           */
/*  FMinute - extract minute from a time                       */
/*  FTime - create a time from hour and minute                 */
/*                                                             */
/***************************************************************/
static int FHour(func_info *info)
{
    int v;
    if (!HASTIME(ARG(0))) return E_BAD_TYPE;
    v = TIMEPART(ARG(0));
    RetVal.type = INT_TYPE;
    RETVAL = v / 60;
    return OK;
}

static int FMinute(func_info *info)
{
    int v;
    if (!HASTIME(ARG(0))) return E_BAD_TYPE;
    v = TIMEPART(ARG(0));
    RetVal.type = INT_TYPE;
    RETVAL = v % 60;
    return OK;
}

static int FTime(func_info *info)
{
    int h, m;

    if (ARG(0).type != INT_TYPE || ARG(1).type != INT_TYPE) return E_BAD_TYPE;

    h = ARGV(0);
    m = ARGV(1);
    if (h<0 || m<0) return E_2LOW;
    if (h>23 || m>59) return E_2HIGH;
    RetVal.type = TIME_TYPE;
    RETVAL = h*60 + m;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FAbs - absolute value                                      */
/*  FSgn - signum function                                     */
/*                                                             */
/***************************************************************/
static int FAbs(func_info *info)
{
    volatile int v;

    ASSERT_TYPE(0, INT_TYPE);
    v = ARGV(0);
    if (v == INT_MIN) return E_2HIGH;

    RetVal.type = INT_TYPE;
    RETVAL = (v < 0) ? (-v) : v;
    v = RETVAL;

    /* The following test is probably redundant given the test
       for v == INT_MIN above, but I'll leave it in just in case. */
    if (v < 0) return E_2HIGH;
    return OK;
}

static int FSgn(func_info *info)
{
    int v;

    ASSERT_TYPE(0, INT_TYPE);
    v = ARGV(0);
    RetVal.type = INT_TYPE;
    if (v>0) RETVAL = 1;
    else if (v<0) RETVAL = -1;
    else RETVAL = 0;
    return OK;
}

static int parse_color_helper(char const *str, int *r, int *g, int *b)
{
    if (!*str) {
        /* Empty string means "reset to normal" */
        *r = -1;
        *g = -1;
        *b = -1;
        return OK;
    }
    if (sscanf(str, "%d %d %d", r, g, b) != 3) {
        return E_BAD_TYPE;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FAnsicolor - return an ANSI terminal color sequence        */
/*                                                             */
/***************************************************************/
static int FAnsicolor(func_info *info)
{
    int r=0, g=0, b=0, bg=0, clamp=1;
    int status;
    int index = 0;
    bg = 0;
    clamp = 1;

    /* If first arg is a string: Parse out the colors */
    if (ARG(0).type == STR_TYPE) {
        /* If first arg is a string: Parse out the colors */
        status = parse_color_helper(ARGSTR(0), &r, &g, &b);
        if (status != 0) {
            return status;
        }
        index = 1;
    } else if (ARG(0).type == INT_TYPE) {
        /* Must be at least three arguments */
        if (Nargs < 3) return E_2FEW_ARGS;
        ASSERT_TYPE(1, INT_TYPE);
        ASSERT_TYPE(2, INT_TYPE);
        r = ARGV(0);
        g = ARGV(1);
        b = ARGV(2);
        index = 3;
    }
    if (r < -1 || g < -1 || b < -1) return E_2LOW;
    if (r > 255 || g > 255 || b > 255) return E_2HIGH;
    /* If any is -1, then all must be -1 */
    if (r == -1 || g == -1 || b == -1) {
        if (r != -1 || g != -1 || b != -1) {
            return E_2LOW;
        }
    }
    if (Nargs > index) {
        ASSERT_TYPE(index, INT_TYPE);
        if (ARGV(index) < 0) return E_2LOW;
        if (ARGV(index) > 1) return E_2HIGH;
        bg = ARGV(index);
        index++;
        if (Nargs > index) {
            ASSERT_TYPE(index, INT_TYPE);
            if (ARGV(index) < 0) return E_2LOW;
            if (ARGV(index) > 1) return E_2HIGH;
            clamp = ARGV(index);
        }
    }

    /* All righ!  We have our parameters; now return the string */
    if (!UseVTColors) {
        /* Not using any colors: Empty strin */
        return RetStrVal("", info);
    }

    if (r < 0) {
        /* Return ANSI "reset to normal" string */
        return RetStrVal(Decolorize(), info);
    }
    return RetStrVal(Colorize(r, g, b, bg, clamp), info);
}

/***************************************************************/
/*                                                             */
/*  FAmpm - return a time as a string with "AM" or "PM" suffix */
/*                                                             */
/***************************************************************/
static int FAmpm(func_info *info)
{
    int h, m;
    int yr=0, mo=0, da=0;

    char const *am = "AM";
    char const *pm = "PM";
    char const *ampm = NULL;

    int include_leading_zero = 0;

    char outbuf[128];

    if (ARG(0).type != DATETIME_TYPE && ARG(0).type != TIME_TYPE) {
        return E_BAD_TYPE;
    }
    if (HASDATE(ARG(0))) {
        FromDSE(DATEPART(ARG(0)), &yr, &mo, &da);
    }
    if (Nargs >= 2) {
        ASSERT_TYPE(1, STR_TYPE);
        am = ARGSTR(1);
        if (Nargs >= 3) {
            ASSERT_TYPE(2, STR_TYPE);
            pm = ARGSTR(2);
            if (Nargs >= 4) {
                ASSERT_TYPE(3, INT_TYPE);
                include_leading_zero = ARGV(3);
            }
        }
    }
    h = TIMEPART(ARG(0)) / 60;
    m = TIMEPART(ARG(0)) % 60;
    if (h <= 11) {
        /* AM */
        if (h == 0) {
            if (ARG(0).type == DATETIME_TYPE) {
                snprintf(outbuf, sizeof(outbuf), "%04d%c%02d%c%02d%c12%c%02d", yr, DateSep, mo+1, DateSep, da, DateTimeSep, TimeSep, m);
            } else {
                snprintf(outbuf, sizeof(outbuf), "12%c%02d", TimeSep, m);
            }
        } else {
            if (ARG(0).type == DATETIME_TYPE) {
                if (include_leading_zero) {
                    snprintf(outbuf, sizeof(outbuf), "%04d%c%02d%c%02d%c%02d%c%02d", yr, DateSep, mo+1, DateSep, da, DateTimeSep, h, TimeSep, m);
                } else {
                    snprintf(outbuf, sizeof(outbuf), "%04d%c%02d%c%02d%c%d%c%02d", yr, DateSep, mo+1, DateSep, da, DateTimeSep, h, TimeSep, m);
                }
            } else {
                if (include_leading_zero) {
                    snprintf(outbuf, sizeof(outbuf), "%02d%c%02d", h, TimeSep, m);
                } else {
                    snprintf(outbuf, sizeof(outbuf), "%d%c%02d", h, TimeSep, m);
                }
            }
        }
        ampm = am;
    } else {
        if (h > 12) {
            h -= 12;
        }
        if (ARG(0).type == DATETIME_TYPE) {
            if (include_leading_zero) {
                snprintf(outbuf, sizeof(outbuf), "%04d%c%02d%c%02d%c%02d%c%02d", yr, DateSep, mo+1, DateSep, da, DateTimeSep, h, TimeSep, m);
            } else {
                snprintf(outbuf, sizeof(outbuf), "%04d%c%02d%c%02d%c%d%c%02d", yr, DateSep, mo+1, DateSep, da, DateTimeSep, h, TimeSep, m);
            }
        } else {
            if (include_leading_zero) {
                snprintf(outbuf, sizeof(outbuf), "%02d%c%02d", h, TimeSep, m);
            } else {
                snprintf(outbuf, sizeof(outbuf), "%d%c%02d", h, TimeSep, m);
            }
        }
        ampm = pm;
    }
    RetVal.type = STR_TYPE;
    RetVal.v.str = malloc(strlen(outbuf) + strlen(ampm) + 1);
    if (!RetVal.v.str) {
        RetVal.type = ERR_TYPE;
        return E_NO_MEM;
    }
    strcpy(RetVal.v.str, outbuf);
    strcat(RetVal.v.str, ampm);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FOrd - returns a string containing ordinal number.         */
/*                                                             */
/*  EG - ord(2) == "2nd", etc.                                 */
/*                                                             */
/***************************************************************/
static int FOrd(func_info *info)
{
    int t, u, v;
    char const *s;

    char buf[32];

    ASSERT_TYPE(0, INT_TYPE);

    v = ARGV(0);
    if (v < 0) {
        t = (-v) % 100;
    } else {
        t = v % 100;
    }
    u = t % 10;
    s = "th";
    if (u == 1 && t != 11) s = "st";
    if (u == 2 && t != 12) s = "nd";
    if (u == 3 && t != 13) s = "rd";
    snprintf(buf, sizeof(buf), "%d%s", v, s);
    return RetStrVal(buf, info);
}

/***************************************************************/
/*                                                             */
/*  FPad - Pad a string to min length                          */
/*                                                             */
/*  pad("1", "0", 4) --> "0001"                                */
/*  pad("1", "0", 4, 1) --> "1000"                             */
/*  pad("foo", "bar", 7) -> "barbfoo"                          */
/*                                                             */
/***************************************************************/
static int FPad(func_info *info)
{
    int r;
    char *s;
    DynamicBuffer dbuf;
    size_t len;
    size_t wantlen;
    size_t i;

    ASSERT_TYPE(1, STR_TYPE);
    ASSERT_TYPE(2, INT_TYPE);
    if (Nargs == 4) {
        ASSERT_TYPE(3, INT_TYPE);
    }

    if (ARG(0).type != STR_TYPE) {
        r = DoCoerce(STR_TYPE, &ARG(0));
        if (r != OK) return r;
    }

    wantlen = ARGV(2);
    len = strlen(ARGSTR(0));
    if (len >= wantlen) {
        DCOPYVAL(RetVal, ARG(0));
        return OK;
    }

    if (strlen(ARGSTR(1)) == 0) {
        return E_BAD_TYPE;
    }

    if (MaxStringLen > 0 && wantlen > (size_t) MaxStringLen) {
        return E_STRING_TOO_LONG;
    }

    DBufInit(&dbuf);
    s = ARGSTR(1);
    if (Nargs < 4 || !ARGV(3)) {
        /* Pad on the LEFT */
        for (i=0; i<wantlen-len; i++) {
            if (DBufPutc(&dbuf, *s++) != OK) {
                DBufFree(&dbuf);
                return E_NO_MEM;
            }
            if (!*s) s = ARGSTR(1);
        }
        if (DBufPuts(&dbuf, ARGSTR(0)) != OK) {
                DBufFree(&dbuf);
                return E_NO_MEM;
        }
    } else {
        /* Pad on the RIGHT */
        if (DBufPuts(&dbuf, ARGSTR(0)) != OK) {
            DBufFree(&dbuf);
            return E_NO_MEM;
        }
        for (i=0; i<wantlen-len; i++) {
            if (DBufPutc(&dbuf, *s++) != OK) {
                DBufFree(&dbuf);
                return E_NO_MEM;
            }
            if (!*s) s = ARGSTR(1);
        }
    }
    r = RetStrVal(DBufValue(&dbuf), info);
    DBufFree(&dbuf);
    return r;
}


/***************************************************************/
/*                                                             */
/*  FPlural - pluralization function                           */
/*                                                             */
/*  plural(n) -->  "" or "s"                                   */
/*  plural(n, str) --> "str" or "strs"                         */
/*  plural(n, str1, str2) --> "str1" or "str2"                 */
/*                                                             */
/***************************************************************/
static int FPlural(func_info *info)
{
    ASSERT_TYPE(0, INT_TYPE);

    switch(Nargs) {
    case 1:
        if (ARGV(0) == 1) return RetStrVal("", info);
        else return RetStrVal("s", info);

    case 2:
        ASSERT_TYPE(1, STR_TYPE);
        if (ARGV(0) == 1) {
            DCOPYVAL(RetVal, ARG(1));
            return OK;
        }
        RetVal.type = STR_TYPE;
        RetVal.v.str = malloc(strlen(ARGSTR(1))+2);
        if (!RetVal.v.str) {
            RetVal.type = ERR_TYPE;
            return E_NO_MEM;
        }
        strcpy(RetVal.v.str, ARGSTR(1));
        strcat(RetVal.v.str, "s");
        return OK;

    default:
        if (ARG(1).type != STR_TYPE || ARG(2).type != STR_TYPE)
            return E_BAD_TYPE;
        if (ARGV(0) == 1) DCOPYVAL(RetVal, ARG(1));
        else DCOPYVAL(RetVal, ARG(2));
        return OK;
    }
}

/***************************************************************/
/*                                                             */
/*  FIsconst                                                   */
/*  Return 1 if the first arg is constant; 0 otherwise         */
/*                                                             */
/***************************************************************/
static int FIsconst(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    Value junk;
    int my_nonconst;
    DynamicBuffer DebugBuf;
    int r;

    UNUSED(nonconst);
    DBG(DBufInit(&DebugBuf));
    DBG(PUT("isconst("));

    my_nonconst = 0;
    r = evaluate_expr_node(node->child, locals, &junk, &my_nonconst);
    if (r != OK) {
        DBG(DBufFree(&DebugBuf));
        return r;
    }
    ans->type = INT_TYPE;
    ans->v.val = (my_nonconst ? 0 : 1);
    DBG(PUT(PrintValue(&junk, NULL)));
    if (DebugFlag & DB_PRTEXPR) {
        PUT(") => ");
        PUT(PrintValue(ans, NULL));
        OUT();
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FIsAny                                                     */
/*  Return 1 if the first arg equals any subsequent arg, 0     */
/*  otherwise.                                                 */
/*                                                             */
/***************************************************************/
static int FIsAny(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    DynamicBuffer DebugBuf;
    expr_node *cur;
    int r;

    Value v;
    Value candidate;

    ans->type = INT_TYPE;
    ans->v.val = 0;

    DBG(DBufInit(&DebugBuf));
    DBG(PUT("isany("));

    cur = node->child;
    r = evaluate_expr_node(cur, locals, &v, nonconst);
    if (r != OK) {
        DBG(DBufFree(&DebugBuf));
        return r;
    }
    DBG(PUT(PrintValue(&v, NULL)));
    while(cur->sibling) {
        cur = cur->sibling;
        r = evaluate_expr_node(cur, locals, &candidate, nonconst);
        if (r != OK) {
            DestroyValue(v);
            DBG(DBufFree(&DebugBuf));
            return r;
        }
        DBG(PUT(", "));
        DBG(PUT(PrintValue(&candidate, NULL)));
        if (candidate.type != v.type) {
            DestroyValue(candidate);
            continue;
        }
        if (v.type == STR_TYPE) {
            if (strcmp(v.v.str, candidate.v.str)) {
                DestroyValue(candidate);
                continue;
            }
        } else {
            if (v.v.val != candidate.v.val) {
                DestroyValue(candidate);
                continue;
            }
        }
        DestroyValue(candidate);
        ans->v.val = 1;
        break;
    }
    DestroyValue(v);
    if (DebugFlag & DB_PRTEXPR) {
        while(cur->sibling) {
            cur = cur->sibling;
            PUT(", ?");
        }
        PUT(") => ");
        PUT(PrintValue(ans, NULL));
        OUT();
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FCatch                                                     */
/*  Evaluate the first argument.  If no error occurs, return   */
/*  the result.  If an error occurs, return the valie of the   */
/*  second argument.                                           */
/*                                                             */
/***************************************************************/
static int FCatch(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    DynamicBuffer DebugBuf;
    expr_node *cur;
    int r;
    int old_suppress;

    DBG(DBufInit(&DebugBuf));
    DBG(PUT("catch("));
    cur = node->child;

    old_suppress = SuppressErrorOutputInCatch;
    SuppressErrorOutputInCatch = 1;
    r = evaluate_expr_node(cur, locals, ans, nonconst);
    SuppressErrorOutputInCatch = old_suppress;

    if (r == OK) {
        if (DebugFlag & DB_PRTEXPR) {
            PUT(PrintValue(ans, NULL));
            PUT(", ?) => ");
            PUT(PrintValue(ans, NULL));
            OUT();
        }
        return r;
    }

    /* Save the catch error */
    LastCatchError = r;
    if (DebugFlag & DB_PRTEXPR) {
        PUT("*");
        PUT(GetErr(r));
        PUT("*, ");
    }
    r = evaluate_expr_node(cur->sibling, locals, ans, nonconst);
    if (r == OK) {
        if (DebugFlag & DB_PRTEXPR) {
            PUT(PrintValue(ans, NULL));
            PUT(") => ");
            PUT(PrintValue(ans, NULL));
            OUT();
        }
        return r;
    }
    if (DebugFlag & DB_PRTEXPR) {
        PUT("*");
        PUT(GetErr(r));
        PUT("*) => ");
        PUT(GetErr(r));
        OUT();
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  FCatchErr                                                  */
/*  Return (as a string) the English error thrown by the last  */
/*  catch() expression that errored out.                       */
/*                                                             */
/***************************************************************/
static int FCatchErr(func_info *info)
{
    return RetStrVal(GetEnglishErr(LastCatchError), info);
}

/***************************************************************/
/*                                                             */
/*  FChoose                                                    */
/*  Choose the nth value from a list of value.  If n<1, choose */
/*  first.  If n>N, choose Nth value.  Indexes always start    */
/*  from 1.                                                    */
/*                                                             */
/***************************************************************/
static int FChoose(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    DynamicBuffer DebugBuf;
    expr_node *cur;
    int r;
    int n;
    int nargs = node->num_kids;
    Value v;
    DBG(DBufInit(&DebugBuf));
    DBG(PUT("choose("));

    cur = node->child;
    r = evaluate_expr_node(cur, locals, &v, nonconst);
    if (r != OK) {
        DBG(DBufFree(&DebugBuf));
        return r;
    }
    DBG(PUT(PrintValue(&v, NULL)));
    if (v.type != INT_TYPE) {
        if (DebugFlag & DB_PRTEXPR) {
            cur = cur->sibling;
            while(cur) {
                PUT(", ?");
                cur = cur->sibling;
            }
            PUT(") => ");
            PUT(GetErr(E_BAD_TYPE));
            OUT();
        }
        Eprint("choose(): %s", GetErr(E_BAD_TYPE));
        return E_BAD_TYPE;
    }
    n = v.v.val;
    if (n < 1) n = 1;
    if (n > nargs-1) n = nargs-1;

    while(n--) {
        cur = cur->sibling;
        DBG(if (n) { PUT(", ?"); });
        if (!cur) return E_SWERR; /* Should not happen! */
    }
    r = evaluate_expr_node(cur, locals, ans, nonconst);
    if (r != OK) {
        DBG(DBufFree(&DebugBuf));
        return r;
    }
    if (DebugFlag & DB_PRTEXPR) {
        PUT(", ");
        PUT(PrintValue(ans, NULL));
        cur = cur->sibling;
        while(cur) {
            PUT(", ?");
            cur = cur->sibling;
        }
        PUT(") => ");
        PUT(PrintValue(ans, NULL));
        OUT();
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FVersion - version of Remind                               */
/*                                                             */
/***************************************************************/
static int FVersion(func_info *info)
{
    return RetStrVal(VERSION, info);
}

/***************************************************************/
/*                                                             */
/*  FOstype - the type of operating system                     */
/*  (UNIX, OS/2, or MSDOS)                                     */
/*                                                             */
/***************************************************************/
static int FOstype(func_info *info)
{
    return RetStrVal("UNIX", info);
}

/***************************************************************/
/*                                                             */
/*  FShellescape - escape shell meta-characters                */
/*                                                             */
/***************************************************************/
static int FShellescape(func_info *info)
{
    DynamicBuffer buf;
    int r;

    ASSERT_TYPE(0, STR_TYPE);
    DBufInit (&buf);
    if (ShellEscape(ARG(0).v.str, &buf) != OK) {
        DBufFree(&buf);
        return E_NO_MEM;
    }

    r = RetStrVal(DBufValue(&buf), info);
    DBufFree(&buf);
    return r;
}

/***************************************************************/
/*                                                             */
/*  FUpper - convert string to upper-case                      */
/*  FLower - convert string to lower-case                      */
/*                                                             */
/***************************************************************/
static int FUpper(func_info *info)
{
    char *s;

    ASSERT_TYPE(0, STR_TYPE);
    DCOPYVAL(RetVal, ARG(0));
    s = RetVal.v.str;
    while (*s) {
        *s = UPPER(*s);
        s++;
    }
    return OK;
}

static int FLower(func_info *info)
{
    char *s;

    ASSERT_TYPE(0, STR_TYPE);
    DCOPYVAL(RetVal, ARG(0));
    s = RetVal.v.str;
    while (*s) {
        *s = LOWER(*s);
        s++;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FStdout - return the type of file descriptor for stdout    */
/*                                                             */
/***************************************************************/
static int FStdout(func_info *info)
{
    struct stat statbuf;
    int r;

    if (isatty(STDOUT_FILENO)) {
        return RetStrVal("TTY", info);
    }
    if (fstat(STDOUT_FILENO, &statbuf) < 0) {
        return RetStrVal("UNKNOWN", info);
    }
    switch(statbuf.st_mode & S_IFMT) {
    case S_IFBLK:  r = RetStrVal("BLOCKDEV", info); break;
    case S_IFCHR:  r = RetStrVal("CHARDEV", info);  break;
    case S_IFDIR:  r = RetStrVal("DIR",info);       break;
    case S_IFIFO:  r = RetStrVal("PIPE",info);      break;
    case S_IFLNK:  r = RetStrVal("SYMLINK", info);  break;
    case S_IFREG:  r = RetStrVal("FILE",info);      break;
    case S_IFSOCK: r = RetStrVal("SOCKET", info);   break;
    default:       r = RetStrVal("UNKNOWN", info);  break;
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  FToday - return the system's notion of "today"             */
/*  Frealtoday - return today's date as read from OS.          */
/*  FNow - return the system time (or time on cmd line.)       */
/*  FRealnow - return the true system time                     */
/*                                                             */
/***************************************************************/
static int FToday(func_info *info)
{
    RetVal.type = DATE_TYPE;
    RETVAL = DSEToday;
    return OK;
}

static int FRealtoday(func_info *info)
{
    RetVal.type = DATE_TYPE;
    RETVAL = RealToday;
    return OK;
}

static int FNow(func_info *info)
{
    RetVal.type = TIME_TYPE;
    RETVAL = MinutesPastMidnight(0);
    return OK;
}

static int FRealnow(func_info *info)
{
    RetVal.type = TIME_TYPE;
    RETVAL = MinutesPastMidnight(1);
    return OK;
}

static int FCurrent(func_info *info)
{
    RetVal.type = DATETIME_TYPE;
    RETVAL = DSEToday * MINUTES_PER_DAY + MinutesPastMidnight(0);
    return OK;
}

static int FRealCurrent(func_info *info)
{
    RetVal.type = DATETIME_TYPE;
    RETVAL = RealToday * MINUTES_PER_DAY + MinutesPastMidnight(1);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FGetenv - get the value of an environment variable.        */
/*                                                             */
/***************************************************************/
static int FGetenv(func_info *info)
{
    ASSERT_TYPE(0, STR_TYPE);
    return RetStrVal(getenv(ARGSTR(0)), info);
}

/***************************************************************/
/*                                                             */
/*  FValue                                                     */
/*                                                             */
/*  Get the value of a variable.  If a second arg is supplied, */
/*  it is returned if variable is undefined.                   */
/*                                                             */
/***************************************************************/
static int FValue(func_info *info)
{
    Var *v;

    ASSERT_TYPE(0, STR_TYPE);
    switch(Nargs) {
    case 1:
        return GetVarValue(ARGSTR(0), &RetVal);

    case 2:
        v = FindVar(ARGSTR(0), 0);
        if (!v) {
            DCOPYVAL(RetVal, ARG(1));
            return OK;
        } else {
            return CopyValue(&RetVal, &v->v);
        }
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FDefined                                                   */
/*                                                             */
/*  Return 1 if a variable is defined, 0 if it is not.         */
/*                                                             */
/***************************************************************/
static int FDefined(func_info *info)
{
    ASSERT_TYPE(0, STR_TYPE);

    RetVal.type = INT_TYPE;

    if (FindVar(ARGSTR(0), 0))
        RETVAL = 1;
    else
        RETVAL = 0;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FTrigdate and FTrigtime                                    */
/*                                                             */
/*  Date and time of last trigger.  These are stored in global */
/*  vars.                                                      */
/*                                                             */
/***************************************************************/
static int FTrigdate(func_info *info)
{
    if (LastTrigValid) {
        RetVal.type = DATE_TYPE;
        RETVAL = LastTriggerDate;
    } else {
        RetVal.type = INT_TYPE;
        RETVAL = 0;
    }
    return OK;
}

static int FTrigbase(func_info *info)
{
    if (LastTrigger.d != NO_DAY &&
        LastTrigger.m != NO_MON &&
        LastTrigger.y != NO_YR) {
        RetVal.type = DATE_TYPE;
        RETVAL = DSE(LastTrigger.y, LastTrigger.m, LastTrigger.d);
    } else {
        RetVal.type = INT_TYPE;
        RETVAL = 0;
    }
    return OK;
}

static int FTrigback(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTrigger.back;
    return OK;
}

static int FTrigdelta(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTrigger.delta;
    return OK;
}

static int FTrigtimedelta(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTimeTrig.delta;
    return OK;
}

static int FTrigtimerep(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTimeTrig.rep;
    return OK;
}

static int FTrigeventduration(func_info *info)
{
    if (LastTrigger.eventduration == NO_TIME) {
        RetVal.type = INT_TYPE;
        RETVAL = -1;
    } else {
        RetVal.type = TIME_TYPE;
        RETVAL = LastTrigger.eventduration;
    }
    return OK;
}

static int FTriginfo(func_info *info)
{
    char const *s;
    ASSERT_TYPE(0, STR_TYPE);
    s = FindTrigInfo(&LastTrigger, ARGSTR(0));
    if (!s) {
        return RetStrVal("", info);
    }
    return RetStrVal(s, info);
}

static int FTrigeventstart(func_info *info)
{
    if (LastTrigger.eventstart == NO_TIME) {
        RetVal.type = INT_TYPE;
        RETVAL = -1;
    } else {
        RetVal.type = DATETIME_TYPE;
        RETVAL = LastTrigger.eventstart;
    }
    return OK;
}

static int FTrigduration(func_info *info)
{
    if (LastTimeTrig.duration == NO_TIME) {
        RetVal.type = INT_TYPE;
        RETVAL = -1;
    } else {
        RetVal.type = TIME_TYPE;
        RETVAL = LastTimeTrig.duration;
    }
    return OK;
}

static int FTrigrep(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTrigger.rep;
    return OK;
}

static int FTrigtags(func_info *info)
{
    return RetStrVal(DBufValue(&(LastTrigger.tags)), info);
}

static int FTrigpriority(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTrigger.priority;
    return OK;
}

static int FTriguntil(func_info *info)
{
    if (LastTrigger.until == NO_UNTIL) {
        RetVal.type = INT_TYPE;
        RETVAL = -1;
    } else {
        RetVal.type = DATE_TYPE;
        RETVAL = LastTrigger.until;
    }
    return OK;
}

static int FTrigscanfrom(func_info *info)
{
    if (LastTrigger.scanfrom == NO_DATE) {
        RetVal.type = INT_TYPE;
        RETVAL = -1;
    } else {
        RetVal.type = DATE_TYPE;
        RETVAL = LastTrigger.scanfrom;
    }
    return OK;
}

static int FTrigfrom(func_info *info)
{
    if (LastTrigger.from == NO_DATE) {
        RetVal.type = INT_TYPE;
        RETVAL = -1;
    } else {
        RetVal.type = DATE_TYPE;
        RETVAL = LastTrigger.from;
    }
    return OK;
}

static int FTrigvalid(func_info *info)
{
    RetVal.type = INT_TYPE;
    RETVAL = LastTrigValid;
    return OK;
}

static int FTrigtime(func_info *info)
{
    if (LastTriggerTime != NO_TIME) {
        RetVal.type = TIME_TYPE;
        RETVAL = LastTriggerTime;
    } else {
        RetVal.type = INT_TYPE;
        RETVAL = 0;
    }
    return OK;
}

static int FTrigdatetime(func_info *info)
{
    if (!LastTrigValid) {
        RetVal.type = INT_TYPE;
        RETVAL = 0;
    } else if (LastTriggerTime != NO_TIME) {
        RetVal.type = DATETIME_TYPE;
        RETVAL = LastTriggerDate * MINUTES_PER_DAY + LastTriggerTime;
    } else {
        RetVal.type = DATE_TYPE;
        RETVAL = LastTriggerDate;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FDaysinmon                                                 */
/*                                                             */
/*  Returns the number of days in mon,yr                       */
/*                                                             */
/***************************************************************/
static int FDaysinmon(func_info *info)
{
    if (ARG(0).type != INT_TYPE || ARG(1).type != INT_TYPE) return E_BAD_TYPE;

    if (ARGV(0) > 12 || ARGV(0) < 1 ||
        ARGV(1) < BASE || ARGV(1) > BASE+YR_RANGE)
        return E_DOMAIN_ERR;

    RetVal.type = INT_TYPE;
    RETVAL = DaysInMonth(ARGV(0)-1, ARGV(1));
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FIsleap                                                    */
/*                                                             */
/*  Return 1 if year is a leap year, zero otherwise.           */
/*                                                             */
/***************************************************************/
static int FIsleap(func_info *info)
{
    int y, m, d;

    if (ARG(0).type != INT_TYPE && !HASDATE(ARG(0))) return E_BAD_TYPE;

    /* If it's a date, extract the year */
    if (HASDATE(ARG(0)))
        FromDSE(DATEPART(ARG(0)), &y, &m, &d);
    else
        y = ARGV(0);

    RetVal.type = INT_TYPE;
    RETVAL = IsLeapYear(y);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FTrigger                                                   */
/*                                                             */
/*  Put out a date in a format suitable for triggering.        */
/*                                                             */
/***************************************************************/
static int FTrigger(func_info *info)
{
    int y, m, d;
    int date, tim;
    char buf[128];

    tim = NO_TIME;
    if (ARG(0).type != DATE_TYPE &&
        ARG(0).type != DATETIME_TYPE) return E_BAD_TYPE;

    if (ARG(0).type == DATE_TYPE) {
        date = ARGV(0);
    } else {
        date = ARGV(0) / MINUTES_PER_DAY;
        tim = ARGV(0) % MINUTES_PER_DAY;
    }

    if (ARG(0).type == DATE_TYPE) {
        if (Nargs > 2) {
            /* Date Time UTCFlag */
            if (ARG(0).type == DATETIME_TYPE) return E_BAD_TYPE;
            ASSERT_TYPE(2, INT_TYPE);
            ASSERT_TYPE(1, TIME_TYPE);
            tim = ARGV(1);
            if (ARGV(2)) {
                UTCToLocal(date, tim, &date, &tim);
            }
        } else if (Nargs > 1) {
            /* Date Time */
            ASSERT_TYPE(1, TIME_TYPE);
            tim = ARGV(1);
        }
    } else {
        if (Nargs > 2) {
            return E_2MANY_ARGS;
        } else if (Nargs > 1) {
            /* DateTime UTCFlag */
            ASSERT_TYPE(1, INT_TYPE);
            if (ARGV(1)) {
                UTCToLocal(date, tim, &date, &tim);
            }
        }
    }

    FromDSE(date, &y, &m, &d);
    if (tim != NO_TIME) {
        snprintf(buf, sizeof(buf), "%d %s %d AT %02d:%02d", d, MonthName[m], y,
                tim/60, tim%60);
    } else {
        snprintf(buf, sizeof(buf), "%d %s %d", d, MonthName[m], y);
    }
    return RetStrVal(buf, info);
}

/***************************************************************/
/*                                                             */
/*  FShell                                                     */
/*                                                             */
/*  The shell function.                                        */
/*                                                             */
/*  If run is disabled, will not be executed.                  */
/*                                                             */
/***************************************************************/
static int FShell(func_info *info)
{
    DynamicBuffer buf;
    int ch, r;
    FILE *fp;

    /* For compatibility with previous versions of Remind, which
       used a static buffer for reading results from shell() command */
    int maxlen = 511;

    DBufInit(&buf);
    if (RunDisabled) return E_RUN_DISABLED;
    ASSERT_TYPE(0, STR_TYPE);
    if (Nargs >= 2) {
        ASSERT_TYPE(1, INT_TYPE);
        maxlen = ARGV(1);
    }

    /* Don't allow maxlen to exceed the maximum length of
       a string variable */
    if (MaxStringLen > 0) {
        if (maxlen <= 0 || maxlen > MaxStringLen) {
            maxlen = MaxStringLen;
        }
    }

    fp = popen(ARGSTR(0), "r");
    if (!fp) return E_IO_ERR;
    while (1) {
        ch = getc(fp);
        if (ch == EOF) {
            break;
        }
        if (isspace(ch)) ch = ' ';
        if (DBufPutc(&buf, (char) ch) != OK) {
            pclose(fp);
            DBufFree(&buf);
            return E_NO_MEM;
        }
        if (maxlen > 0 && DBufLen(&buf) >= (size_t) maxlen) {
            break;
        }
    }

    /* Delete trailing newline (converted to space) */
    if (DBufLen(&buf) && DBufValue(&buf)[DBufLen(&buf)-1] == ' ') {
        DBufValue(&buf)[DBufLen(&buf)-1] = 0;
    }

    /* XXX Should we consume remaining output from cmd? */

    pclose(fp);
    r = RetStrVal(DBufValue(&buf), info);
    DBufFree(&buf);
    return r;
}

/***************************************************************/
/*                                                             */
/*  FIsomitted                                                 */
/*                                                             */
/*  Is a date omitted?                                         */
/*                                                             */
/***************************************************************/
static int FIsomitted(func_info *info)
{
    int r;
    if (!HASDATE(ARG(0))) return E_BAD_TYPE;

    RetVal.type = INT_TYPE;
    r = IsOmitted(DATEPART(ARG(0)), 0, NULL, &RETVAL);
    return r;
}

/***************************************************************/
/*                                                             */
/*  FSubstr                                                    */
/*                                                             */
/*  The substr function.  We destroy the value on the stack.   */
/*                                                             */
/***************************************************************/
static int FSubstr(func_info *info)
{
    char *s;
    char const *t;
    int start, end;

    if (ARG(0).type != STR_TYPE || ARG(1).type != INT_TYPE) return E_BAD_TYPE;
    if (Nargs == 3 && ARG(2).type != INT_TYPE) return E_BAD_TYPE;

    s = ARGSTR(0);
    start = 1;
    while (start < ARGV(1)) {
        if (!*s) break;
        s++;
        start++;
    }
    if (Nargs == 2 || !*s) return RetStrVal(s, info);
    end = start;
    t = s;
    while (end <= ARGV(2)) {
        if (!*s) break;
        s++;
        end++;
    }
    *s = 0;
    return RetStrVal(t, info);
}

/***************************************************************/
/*                                                             */
/*  FIndex                                                     */
/*                                                             */
/*  The index of one string embedded in another.               */
/*                                                             */
/***************************************************************/
static int FIndex(func_info *info)
{
    char const *s;
    int start;

    if (ARG(0).type != STR_TYPE || ARG(1).type != STR_TYPE ||
        (Nargs == 3 && ARG(2).type != INT_TYPE)) return E_BAD_TYPE;

    s = ARGSTR(0);

/* If 3 args, bump up the start */
    if (Nargs == 3) {
        start = 1;
        while (start < ARGV(2)) {
            if (!*s) break;
            s++;
            start++;
        }
    }

/* Find the string */
    s = strstr(s, ARGSTR(1));
    RetVal.type = INT_TYPE;
    if (!s) {
        RETVAL = 0;
        return OK;
    }
    RETVAL = (s - ARGSTR(0)) + 1;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FIif                                                       */
/*                                                             */
/*  The IIF function.  Uses new-style evaluation               */
/*                                                             */
/***************************************************************/
static int FIif(expr_node *node, Value *locals, Value *ans, int *nonconst)
{
    int r;
    int done;
    Value v;
    expr_node *cur;
    DynamicBuffer DebugBuf;

    DBG(DBufInit(&DebugBuf));
    DBG(PUT("iif("));
    cur = node->child;

    if (!(node->num_kids % 2)) {
        if (DebugFlag & DB_PRTEXPR) {
            r = 0;
            while(cur) {
                if (r) PUT(", ");
                r=1;
                PUT("?");
                cur = cur->sibling;
            }
            PUT(") => ");
            PUT(GetErr(E_IIF_ODD));
            OUT();
        }
        return E_IIF_ODD;
    }


    done = 0;
    while(cur->sibling) {
        r = evaluate_expr_node(cur, locals, &v, nonconst);
        if (r != OK) {
            DBG(DBufFree(&DebugBuf));
            return r;
        }
        if (DebugFlag & DB_PRTEXPR) {
            if (done) PUT(", ");
            done = 1;
            PUT(PrintValue(&v, NULL));
        }

        if (truthy(&v)) {
            r = evaluate_expr_node(cur->sibling, locals, ans, nonconst);
            if (r == OK && (DebugFlag & DB_PRTEXPR)) {
                PUT(", ");
                PUT(PrintValue(ans, NULL));
                cur = cur->sibling->sibling;
                while(cur) {
                    PUT(", ?");
                    cur = cur->sibling;
                }
                PUT(") => ");
                PUT(PrintValue(ans, NULL));
                OUT();
            }
            DBG(DBufFree(&DebugBuf));
            return r;
        }
        DBG(PUT(", ?"));
        cur = cur->sibling->sibling;
    }

    /* Return the last arg */
    r = evaluate_expr_node(cur, locals, ans, nonconst);
    if (DebugFlag & DB_PRTEXPR) {
        if (done) PUT(", ");
        PUT(PrintValue(ans, NULL));
        PUT(") => ");
        PUT(PrintValue(ans, NULL));
        OUT();
    }
    return r;
}

/***************************************************************/
/*                                                             */
/*  FFilename                                                  */
/*                                                             */
/*  Return name of current file                                */
/*                                                             */
/***************************************************************/
static int FFilename(func_info *info)
{
    return RetStrVal(FileName, info);
}

/***************************************************************/
/*                                                             */
/*  FFiledir                                                   */
/*                                                             */
/*  Return directory of current file                           */
/*                                                             */
/***************************************************************/
static int FFiledir(func_info *info)
{
    char *s;
    DynamicBuffer buf;
    int r;

    DBufInit(&buf);

    if (DBufPuts(&buf, FileName) != OK) return E_NO_MEM;
    if (DBufLen(&buf) == 0) {
        DBufFree(&buf);
        return RetStrVal(".", info);
    }

    s = DBufValue(&buf) + DBufLen(&buf) - 1;
    while (s > DBufValue(&buf) && *s != '/') s--;
    if (*s == '/') {
        *s = 0;
        r = RetStrVal(DBufValue(&buf), info);
    } else r = RetStrVal(".", info);
    DBufFree(&buf);
    return r;
}
/***************************************************************/
/*                                                             */
/*  FAccess                                                    */
/*                                                             */
/*  The UNIX access() system call.                             */
/*                                                             */
/***************************************************************/
static int FAccess(func_info *info)
{
    int amode;
    char const *s;

    if (ARG(0).type != STR_TYPE ||
        (ARG(1).type != INT_TYPE && ARG(1).type != STR_TYPE)) return E_BAD_TYPE;

    if (ARG(1).type == INT_TYPE) amode = ARGV(1);
    else {
        amode = 0;
        s = ARGSTR(1);
        while (*s) {
            switch(*s++) {
            case 'r':
            case 'R': amode |= R_OK; break;
            case 'w':
            case 'W': amode |= W_OK; break;
            case 'x':
            case 'X': amode |= X_OK; break;
            }
        }
    }
    RetVal.type = INT_TYPE;
    RETVAL = access(ARGSTR(0), amode);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FTypeof                                                    */
/*                                                             */
/*  Implement the typeof() function.                           */
/*                                                             */
/***************************************************************/
static int FTypeof(func_info *info)
{
    switch(ARG(0).type) {
    case INT_TYPE:  return RetStrVal("INT", info);
    case DATE_TYPE: return RetStrVal("DATE", info);
    case TIME_TYPE:  return RetStrVal("TIME", info);
    case STR_TYPE:  return RetStrVal("STRING", info);
    case DATETIME_TYPE: return RetStrVal("DATETIME", info);
    default:        return RetStrVal("ERR", info);
    }
}

/***************************************************************/
/*                                                             */
/*  FLanguage                                                  */
/*                                                             */
/*  Implement the language() function.                         */
/*                                                             */
/***************************************************************/
static int FLanguage(func_info *info)
{
    return RetStrVal("English", info);
}

/***************************************************************/
/*                                                             */
/*  FArgs                                                      */
/*                                                             */
/*  Implement the args() function.                             */
/*                                                             */
/***************************************************************/
static int FArgs(func_info *info)
{
    ASSERT_TYPE(0, STR_TYPE);
    RetVal.type = INT_TYPE;
    strtolower(ARGSTR(0));
    RETVAL = UserFuncExists(ARGSTR(0));
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FDosubst                                                   */
/*                                                             */
/*  Implement the dosubst() function.                          */
/*                                                             */
/***************************************************************/
static int FDosubst(func_info *info)
{
    int dse, tim, r;
    DynamicBuffer buf;

    DBufInit(&buf);

    dse = NO_DATE;
    tim = NO_TIME;
    ASSERT_TYPE(0, STR_TYPE);
    if (Nargs >= 2) {
        if (ARG(1).type == DATETIME_TYPE) {
            dse = DATEPART(ARG(1));
            tim = TIMEPART(ARG(1));
        } else {
            ASSERT_TYPE(1, DATE_TYPE);
            dse = ARGV(1);
        }
        if (Nargs >= 3) {
            if (ARG(1).type == DATETIME_TYPE) {
                return E_2MANY_ARGS;
            }
            ASSERT_TYPE(2, TIME_TYPE);
            tim = ARGV(2);
        }
    }

    if ((r=DoSubstFromString(ARGSTR(0), &buf, dse, tim))) return r;
    r = RetStrVal(DBufValue(&buf), info);
    DBufFree(&buf);
    return r;
}

/***************************************************************/
/*                                                             */
/*  FHebdate                                                   */
/*  FHebday                                                    */
/*  FHebmon                                                    */
/*  FHebyear                                                   */
/*                                                             */
/*  Hebrew calendar support functions                          */
/*                                                             */
/***************************************************************/
static int FHebdate(func_info *info)
{
    int year, day, mon, jahr;
    int mout, dout;
    int ans, r;
    int adarbehave;

    if (ARG(0).type != INT_TYPE || ARG(1).type != STR_TYPE) return E_BAD_TYPE;
    day = ARGV(0);
    mon = HebNameToNum(ARGSTR(1));
    if (mon < 0) return E_BAD_HEBDATE;
    if (Nargs == 2) {
        r = GetNextHebrewDate(DSEToday, mon, day, 0, 0, &ans);
        if (r) return r;
        RetVal.type = DATE_TYPE;
        RETVAL = ans;
        return OK;
    }
    if (Nargs == 5) {
        ASSERT_TYPE(4, INT_TYPE);
        adarbehave = ARGV(4);
        if (adarbehave < 0) return E_2LOW;
        if (adarbehave > 2) return E_2HIGH;
    } else adarbehave = 0;

    if (Nargs >= 4) {
        ASSERT_TYPE(3, INT_TYPE);
        jahr = ARGV(3);
        if (jahr < 0) return E_2LOW;
        if (jahr > 2) {
            r = ComputeJahr(jahr, mon, day, &jahr);
            if (r) return r;
        }
    } else jahr = 0;


    if (ARG(2).type == INT_TYPE) {
        year = ARGV(2);
        r = GetValidHebDate(year, mon, day, 0, &mout, &dout, jahr);
        if (r) return r;
        r = HebToDSE(year, mout, dout);
        if (r<0) return E_DATE_OVER;
        RETVAL = r;
        RetVal.type = DATE_TYPE;
        return OK;
    } else if (HASDATE(ARG(2))) {
        r = GetNextHebrewDate(DATEPART(ARG(2)), mon, day, jahr, adarbehave, &ans);
        if (r) return r;
        RETVAL = ans;
        RetVal.type = DATE_TYPE;
        return OK;
    } else return E_BAD_TYPE;
}

static int FHebday(func_info *info)
{
    int y, m, d, v;

    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));
    if (v == CacheHebDse)
        d = CacheHebDay;
    else {
        DSEToHeb(v, &y, &m, &d);
        CacheHebDse = v;
        CacheHebYear = y;
        CacheHebMon = m;
        CacheHebDay = d;
    }
    RetVal.type = INT_TYPE;
    RETVAL = d;
    return OK;
}

static int FHebmon(func_info *info)
{
    int y, m, d, v;

    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));

    if (v == CacheHebDse) {
        m = CacheHebMon;
        y = CacheHebYear;
    } else {
        DSEToHeb(v, &y, &m, &d);
        CacheHebDse = v;
        CacheHebYear = y;
        CacheHebMon = m;
        CacheHebDay = d;
    }
    return RetStrVal(HebMonthName(m, y), info);
}

static int FHebyear(func_info *info)
{
    int y, m, d, v;

    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    v = DATEPART(ARG(0));

    if (v == CacheHebDse)
        y = CacheHebYear;
    else {
        DSEToHeb(v, &y, &m, &d);
        CacheHebDse = v;
        CacheHebYear = y;
        CacheHebMon = m;
        CacheHebDay = d;
    }
    RetVal.type = INT_TYPE;
    RETVAL = y;
    return OK;
}

/****************************************************************/
/*                                                              */
/* escape - escape special characters with "\xx" sequences      */
/*                                                              */
/****************************************************************/
static int FEscape(func_info *info)
{
    DynamicBuffer dbuf;
    char const *s;
    char hexbuf[16];
    int r;
    int include_quotes = 0;
    ASSERT_TYPE(0, STR_TYPE);
    if (Nargs >= 2) {
        ASSERT_TYPE(1, INT_TYPE);
        include_quotes = ARGV(1);
    }

    DBufInit(&dbuf);
    if (include_quotes) {
        r = DBufPutc(&dbuf, '"');
        if (r != OK) {
            DBufFree(&dbuf);
            return r;
        }
    }
    s = ARGSTR(0);
    while(*s) {
        switch(*s) {
        case '\a':
            r = DBufPuts(&dbuf, "\\a");
            break;
        case '\b':
            r = DBufPuts(&dbuf, "\\b");
            break;
        case '\f':
            r = DBufPuts(&dbuf, "\\f");
            break;
        case '\n':
            r = DBufPuts(&dbuf, "\\n");
            break;
        case '\r':
            r = DBufPuts(&dbuf, "\\r");
            break;
        case '\t':
            r = DBufPuts(&dbuf, "\\t");
            break;
        case '\v':
            r = DBufPuts(&dbuf, "\\v");
            break;
        case '\\':
            r = DBufPuts(&dbuf, "\\\\");
            break;
        case '"':
            r = DBufPuts(&dbuf, "\\\"");
            break;
        default:
            if ((*s > 0 && *s < ' ') || *s == 0x7f) {
                snprintf(hexbuf, sizeof(hexbuf), "\\x%02x", (unsigned int) *s);
                r = DBufPuts(&dbuf, hexbuf);
            } else {
                r = DBufPutc(&dbuf, *s);
            }
            break;
        }
        if (r != OK) {
            DBufFree(&dbuf);
            return r;
        }
        s++;
    }
    if (include_quotes) {
        r = DBufPutc(&dbuf, '"');
        if (r != OK) {
            DBufFree(&dbuf);
            return r;
        }
    }
    r = RetStrVal(DBufValue(&dbuf), info);
    DBufFree(&dbuf);
    return r;
}

/****************************************************************/
/*                                                              */
/* htmlescape - replace <. > and & by &lt; &gt; and &amp;       */
/*                                                              */
/****************************************************************/
static int FHtmlEscape(func_info *info)
{
    DynamicBuffer dbuf;
    char const *s;
    int r;

    ASSERT_TYPE(0, STR_TYPE);

    DBufInit(&dbuf);

    s = ARGSTR(0);
    while(*s) {
        switch(*s) {
        case '<':
            r = DBufPuts(&dbuf, "&lt;");
            break;

        case '>':
            r = DBufPuts(&dbuf, "&gt;");
            break;

        case '&':
            r = DBufPuts(&dbuf, "&amp;");
            break;

        default:
            r = DBufPutc(&dbuf, *s);
            break;
        }
        if (r != OK) {
            DBufFree(&dbuf);
            return r;
        }
        s++;
    }
    r = RetStrVal(DBufValue(&dbuf), info);
    DBufFree(&dbuf);
    return r;
}

/****************************************************************/
/*                                                              */
/* htmlstriptags - strip out HTML tags from a string            */
/*                                                              */
/****************************************************************/
static int FHtmlStriptags(func_info *info)
{
    DynamicBuffer dbuf;
    char const *s;
    int r = OK;

    int in_tag = 0;
    ASSERT_TYPE(0, STR_TYPE);

    DBufInit(&dbuf);

    s = ARGSTR(0);
    while(*s) {
        if (!in_tag) {
            if (*s == '<') {
                in_tag = 1;
            } else {
                r = DBufPutc(&dbuf, *s);
            }
        } else {
            if (*s == '>') {
                in_tag = 0;
            }
        }
        if (r != OK) {
            DBufFree(&dbuf);
            return r;
        }
        s++;
    }
    r = RetStrVal(DBufValue(&dbuf), info);
    DBufFree(&dbuf);
    return r;
}

/****************************************************************/
/*                                                              */
/*  FEasterdate - calc. easter Sunday from a year.              */
/*                                                              */
/*    from The Art of Computer Programming Vol 1.               */
/*            Fundamental Algorithms                            */
/*    by Donald Knuth.                                          */
/*                                                              */
/* Donated by Michael Salmon - thanks!                          */
/*                                                              */
/* I haven't examined this in detail, but I *think* int         */
/* arithmetic is fine, even on 16-bit machines.                 */
/*                                                              */
/****************************************************************/
static int FEasterdate(func_info *info)
{
    int y, m, d;
    int g, c, x, z, e, n;
    int base;
    if (Nargs == 0) {
        base = DSEToday;
        FromDSE(DSEToday, &y, &m, &d);
    } else {
        if (ARG(0).type == INT_TYPE) {
            base = -1;
            y = ARGV(0);
            if (y < BASE) return E_2LOW;
            else if (y > BASE+YR_RANGE) return E_2HIGH;
        } else if (HASDATE(ARG(0))) {
            base = DATEPART(ARG(0));
            FromDSE(DATEPART(ARG(0)), &y, &m, &d);  /* We just want the year */
        } else return E_BAD_TYPE;
    }

    do {
        g = (y % 19) + 1;  /* golden number */
        c = (y / 100) + 1; /* century */
        x = (3 * c)/4 - 12;        /* correction for non-leap year centuries */
        z = (8 * c + 5)/25 - 5;    /* special constant for moon sync */
        d = (5 * y)/4 - x - 10;    /* find sunday */
        e = (11 * g + 20 + z - x) % 30;    /* calc epact */
        if ( e < 0 ) e += 30;
        if ( e == 24 || (e == 25 && g > 11)) e++;
        n = 44 - e;                        /* find full moon */
        if ( n < 21 ) n += 30;     /* after 21st */
        d = n + 7 - (d + n)%7;     /* calc sunday after */
        if (d <= 31) m = 2;
        else
        {
            d = d - 31;
            m = 3;
        }

        RetVal.type = DATE_TYPE;
        RETVAL = DSE(y, m, d);
        y++; } while (base > -1 && RETVAL < base);

    return OK;
}

/****************************************************************/
/*                                                              */
/*  FOrthodoxeaster - calc. Orthodox easter Sunday              */
/*                                                              */
/*  From Meeus, Astronomical Algorithms                         */
/*                                                              */
/****************************************************************/
static int FOrthodoxeaster(func_info *info)
{
    int y, m, d;
    int a, b, c, dd, e, f, dse;
    int base = -1;
    if (Nargs == 0) {
        base = DSEToday;
        FromDSE(DSEToday, &y, &m, &d);
    } else {
        if (ARG(0).type == INT_TYPE) {
            y = ARGV(0);
            if (y < BASE) return E_2LOW;
            else if (y > BASE+YR_RANGE) return E_2HIGH;
        } else if (HASDATE(ARG(0))) {
            base = DATEPART(ARG(0));
            FromDSE(DATEPART(ARG(0)), &y, &m, &d);  /* We just want the year */
        } else return E_BAD_TYPE;
    }

    do {
        a = y % 4;
        b = y % 7;
        c = y % 19;
        dd = (19 * c + 15) % 30;
        e = (2*a + 4*b - dd + 34) % 7;
        f = dd + e + 114;
        m = (f / 31) - 1;
        d = (f % 31) + 1;

        dse = DSE(y, m, d);
        dse += JulianToGregorianOffset(y, m);
        RetVal.type = DATE_TYPE;
        RETVAL = dse;
        y++;
    } while (base > -1 && RETVAL < base);

    return OK;
}
/***************************************************************/
/*                                                             */
/*  FIsdst and FMinsfromutc                                    */
/*                                                             */
/*  Check whether daylight saving time is in effect, and      */
/*  get minutes from UTC.                                      */
/*                                                             */
/***************************************************************/
static int FTimeStuff (int wantmins, func_info *info);
static int FIsdst(func_info *info)
{
    return FTimeStuff(0, info);
}

static int FMinsfromutc(func_info *info)
{
    return FTimeStuff(1, info);
}

static int FTimeStuff(int wantmins, func_info *info)
{
    int dse, tim;
    int mins, dst;

    dse = DSEToday;
    tim = 0;

    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        dse = DATEPART(ARG(0));
        if (HASTIME(ARG(0))) {
            tim = TIMEPART(ARG(0));
        }
        if (Nargs >= 2) {
            if (HASTIME(ARG(0))) return E_2MANY_ARGS;
            ASSERT_TYPE(1, TIME_TYPE);
            tim = ARGV(1);
        }
    }

    if (CalcMinsFromUTC(dse, tim, &mins, &dst)) {
        return E_MKTIME_PROBLEM;
    }
    RetVal.type = INT_TYPE;
    if (wantmins) RETVAL = mins; else RETVAL = dst;

    return OK;
}

static int FTimezone(func_info *info)
{
    int yr, mon, day, hr, min, dse, now;
    struct tm local, *withzone;
    time_t t;
    char buf[64];

    if (Nargs == 0) {
        dse = DSEToday;
        now = MinutesPastMidnight(0);
    } else {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        dse = DATEPART(ARG(0));
        if (HASTIME(ARG(0))) {
            now = TIMEPART(ARG(0));
        } else {
            now = 0;
        }
    }
    FromDSE(dse, &yr, &mon, &day);
    hr = now / 60;
    min = now % 60;

    memset(&local, 0, sizeof(local));
    local.tm_sec = 0;
    local.tm_min = min;
    local.tm_hour = hr;
    local.tm_mday = day;
    local.tm_mon = mon;
    local.tm_year = yr-1900;
    local.tm_isdst = -1;

    t = mktime(&local);
    withzone = localtime(&t);
    buf[0] = 0;
    strftime(buf, sizeof(buf), "%Z", withzone);
    return RetStrVal(buf, info);
}

static int FLocalToUTC(func_info *info)
{
    int yr, mon, day, hr, min, dse;
    time_t loc_t;
    struct tm local, *utc;

    int fold_year = -1;
    int wkday, isleap;

    ASSERT_TYPE(0, DATETIME_TYPE);

    FromDSE(DATEPART(ARG(0)), &yr, &mon, &day);
    hr = TIMEPART(ARG(0))/60;
    min = TIMEPART(ARG(0))%60;

    memset(&local, 0, sizeof(local));
    local.tm_sec = 0;
    local.tm_min = min;
    local.tm_hour = hr;
    local.tm_mday = day;
    local.tm_mon = mon;
    local.tm_year = yr-1900;
    local.tm_isdst = -1;
    loc_t = mktime(&local);
    if (loc_t == -1) {
        /* Try folding the year */
        wkday = DSE(yr, 0, 1) % 7;
        isleap = IsLeapYear(yr);
        fold_year = FoldArray[isleap][wkday];
        memset(&local, 0, sizeof(local));
        local.tm_sec = 0;
        local.tm_min = min;
        local.tm_hour = hr;
        local.tm_mday = day;
        local.tm_mon = mon;
        local.tm_year = fold_year-1900;
        local.tm_isdst = -1;
        loc_t = mktime(&local);
        if (loc_t == -1) {
            /* Still no joy */
            return E_MKTIME_PROBLEM;
        }
    }

    utc = gmtime(&loc_t);

    /* Unfold the year, if necessary */
    if (fold_year > 0) {
        utc->tm_year = yr + utc->tm_year - fold_year; /* The two 1900s cancel */
    }

    dse = DSE(utc->tm_year+1900, utc->tm_mon, utc->tm_mday);
    RetVal.type = DATETIME_TYPE;
    RETVAL = MINUTES_PER_DAY * dse + utc->tm_hour*60 + utc->tm_min;
    return OK;
}

static int UTCToLocalHelper(int datetime, int *ret)
{
    int yr, mon, day, hr, min, dse;
    time_t utc_t;
    struct tm *local, utc;
    char const *old_tz;
    int fold_year = -1;
    int isleap, wkday;

    FromDSE(datetime / MINUTES_PER_DAY, &yr, &mon, &day);
    hr =  (datetime % MINUTES_PER_DAY) / 60;
    min = (datetime % MINUTES_PER_DAY) % 60;

    old_tz = getenv("TZ");
    if (old_tz) {
        old_tz = StrDup(old_tz);
        if (!old_tz) return E_NO_MEM;
    }

    tz_set_tz("UTC");

    memset(&utc, 0, sizeof(utc));
    utc.tm_sec = 0;
    utc.tm_min = min;
    utc.tm_hour = hr;
    utc.tm_mday = day;
    utc.tm_mon = mon;
    utc.tm_year = yr-1900;
    utc.tm_isdst = 0;
    utc_t = mktime(&utc);

    if (utc_t == -1) {
        /* Try folding the year */
        wkday = DSE(yr, 0, 1) % 7;
        isleap = IsLeapYear(yr);
        fold_year = FoldArray[isleap][wkday];
        memset(&utc, 0, sizeof(utc));
        utc.tm_sec = 0;
        utc.tm_min = min;
        utc.tm_hour = hr;
        utc.tm_mday = day;
        utc.tm_mon = mon;
        utc.tm_year = fold_year-1900;
        utc.tm_isdst = 0;
        utc_t = mktime(&utc);
    }
    tz_set_tz(old_tz);
    if (old_tz) {
        free( (void *) old_tz);
    }
    if (utc_t == -1) {
        return E_MKTIME_PROBLEM;
    }

    local = localtime(&utc_t);
    if (fold_year > 0) {
        local->tm_year = yr + local->tm_year - fold_year; /* The two 1900s cancel */
    }
    dse = DSE(local->tm_year+1900, local->tm_mon, local->tm_mday);
    *ret = MINUTES_PER_DAY * dse + local->tm_hour*60 + local->tm_min;
    return OK;
}

static int FUTCToLocal(func_info *info)
{

    int ret;
    int r;

    ASSERT_TYPE(0, DATETIME_TYPE);

    r = UTCToLocalHelper(ARGV(0), &ret);
    if (r != 0) {
        return r;
    }
    RetVal.type = DATETIME_TYPE;
    RETVAL = ret;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  Sunrise and sunset functions.                              */
/*                                                             */
/*  Algorithm from "Almanac for computers for the year 1978"   */
/*  by L. E. Doggett, Nautical Almanac Office, USNO.           */
/*                                                             */
/*  This code also uses some ideas found in programs written   */
/*  by Michael Schwartz and Marc T. Kaufman.                   */
/*                                                             */
/***************************************************************/
#ifdef PI
#undef PI
#endif
#define PI 3.14159265358979323846
#define DEGRAD (PI/180.0)
#define RADDEG (180.0/PI)

static int SunStuff(int rise, double cosz, int dse)
{
    int mins, hours;
    int year, mon, day;

    double M, L, sinDelta, cosDelta, a, a_hr, cosH, t, H, T;
    double latitude, longdeg, UT, local;

/* Get offset from UTC */
    if (CalculateUTC) {
        if (CalcMinsFromUTC(dse, 12*60, &mins, NULL)) {
            Eprint(GetErr(E_MKTIME_PROBLEM));
            return NO_TIME;
        }
    } else mins = MinsFromUTC;

/* Get latitude and longitude */
    longdeg = -Longitude;
    latitude = DEGRAD * Latitude;

    FromDSE(dse, &year, &mon, &day);

/* Following formula on page B6 exactly... */
    t = (double) dse;
    if (rise) {
        t += (6.0 + longdeg/15.0) / 24.0;
    } else {
        t += (18.0 + longdeg/15.0) / 24.0;
    }

/* Mean anomaly of sun starting from 1 Jan 1990 */
/* NOTE: This assumes that BASE = 1990!!! */
#if BASE != 1990
#warning Sun calculations assume a BASE of 1990!
#endif
    t = 0.9856002585 * t;
    M = t + 357.828757; /* In degrees */

    /* Make sure M is in the range [0, 360) */
    M -= (floor(M/360.0) * 360.0);

/* Sun's true longitude */
    L = M + 1.916*sin(DEGRAD*M) + 0.02*sin(2*DEGRAD*M) + 283.07080214;
    if (L > 360.0) L -= 360.0;

/* Tan of sun's right ascension */
    a = RADDEG * atan2(0.91746*sin(DEGRAD*L), cos(DEGRAD*L));
    if (a<0) {
        a += 360.0;
    }

    a_hr = a / 15.0;

/* Sine of sun's declination */
    sinDelta = 0.39782 * sin(DEGRAD*L);
    cosDelta = sqrt(1 - sinDelta*sinDelta);

/* Cosine of sun's local hour angle */
    cosH = (cosz - sinDelta * sin(latitude)) / (cosDelta * cos(latitude));

    if (cosH < -1.0) { /* Summer -- permanent daylight */
        if (rise) return NO_TIME;
        else      return -NO_TIME;
    }
    if (cosH > 1.0) { /* Winter -- permanent darkness */
        if (rise) return -NO_TIME;
        else      return NO_TIME;
    }

    H = RADDEG * acos(cosH);
    if (rise) H = 360.0 - H;

    t -= 360.0*floor(t/360.0);
    T = (H-t) / 15.0 + a_hr - 6.726637276;

    if (T >= 24.0) T -= 24.0;
    else if (T < 0.0) T+= 24.0;

    UT = T + longdeg / 15.0;


    local = UT + (double) mins / 60.0;
    if (local < 0.0) local += 24.0;
    else if (local >= 24.0) local -= 24.0;

    /* Round off local time to nearest minute */
    local = floor(local * 60.0 + 0.5) / 60.0;

    hours = (int) local;
    mins = (int) ((local - hours) * 60.0);

    /* Sometimes, we get roundoff error.  Check for "reasonableness" of
       answer. */
    if (rise) {
        /* Sunrise so close to midnight it wrapped around -- permanent light */
        if (hours >= 23) return NO_TIME;
    } else {
        /* Sunset so close to midnight it wrapped around -- permanent light */
        if (hours <= 1) return -NO_TIME;
    }
    return hours*60 + mins;
}

/***************************************************************/
/*                                                             */
/*  Sunrise and Sunset functions.                              */
/*                                                             */
/***************************************************************/
static int FSun(int rise, func_info *info)
{
    int dse = DSEToday;
    /* Assignment below is not necessary, but it silences
       a GCC warning about a possibly-uninitialized variable */
    double cosz = 0.0;
    int r;

    /* Sun calculations assume BASE is 1990 */
    if (BASE != 1990) {
        return E_SWERR;
    }
    if (rise == 0 || rise == 1) {
    /* Sunrise and sunset : cos(90 degrees + 50 arcminutes) */
    cosz = -0.01454389765158243;
    } else if (rise == 2 || rise == 3) {
    /* Civil twilight: cos(96 degrees) */
        cosz = -0.10452846326765333;
    } else if (rise == 4 || rise == 5) {
    /* Nautical twilight: cos(102 degrees) */
        cosz = -0.20791169081775912;
    } else if (rise == 6 || rise == 7) {
    /* Astronomical twilight: cos(108 degrees) */
        cosz = -0.30901699437494734;
    }
    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        dse = DATEPART(ARG(0));
    }

    r = SunStuff(rise % 2, cosz, dse);
    if (r == NO_TIME) {
        RETVAL = 0;
        RetVal.type = INT_TYPE;
    } else if (r == -NO_TIME) {
        RETVAL = MINUTES_PER_DAY;
        RetVal.type = INT_TYPE;
    } else {
        RETVAL = r;
        RetVal.type = TIME_TYPE;
    }
    return OK;
}

static int FSunrise(func_info *info)
{
    return FSun(1, info);
}
static int FSunset(func_info *info)
{
    return FSun(0, info);
}

static int FDawn(func_info *info)
{
    return FSun(3, info);
}
static int FDusk(func_info *info)
{
    return FSun(2, info);
}

static int FNDawn(func_info *info)
{
    return FSun(5, info);
}
static int FNDusk(func_info *info)
{
    return FSun(4, info);
}

static int FADawn(func_info *info)
{
    return FSun(7, info);
}
static int FADusk(func_info *info)
{
    return FSun(6, info);
}

/***************************************************************/
/*                                                             */
/*  FFiledate                                                  */
/*                                                             */
/*  Return modification date of a file                         */
/*                                                             */
/***************************************************************/
static int FFiledate(func_info *info)
{
    struct stat statbuf;
    struct tm *t1;

    RetVal.type = DATE_TYPE;

    ASSERT_TYPE(0, STR_TYPE);

    if (stat(ARGSTR(0), &statbuf)) {
        RETVAL = 0;
        return OK;
    }

    t1 = localtime(&(statbuf.st_mtime));

    if (t1->tm_year + 1900 < BASE)
        RETVAL=0;
    else
        RETVAL=DSE(t1->tm_year+1900, t1->tm_mon, t1->tm_mday);

    return OK;
}

/***************************************************************/
/*                                                             */
/*  FFiledatetime                                              */
/*                                                             */
/*  Return modification datetime of a file                     */
/*                                                             */
/***************************************************************/
static int FFiledatetime(func_info *info)
{
    struct stat statbuf;
    struct tm *t1;

    RetVal.type = DATETIME_TYPE;

    ASSERT_TYPE(0, STR_TYPE);

    if (stat(ARGSTR(0), &statbuf)) {
        RETVAL = 0;
        return OK;
    }

    t1 = localtime(&(statbuf.st_mtime));

    if (t1->tm_year + 1900 < BASE)
        RETVAL=0;
    else
        RETVAL = MINUTES_PER_DAY * DSE(t1->tm_year+1900, t1->tm_mon, t1->tm_mday) + t1->tm_hour * 60 + t1->tm_min;

    return OK;
}

/***************************************************************/
/*                                                             */
/*  FPsshade                                                   */
/*                                                             */
/*  Canned PostScript code for shading a calendar square       */
/*                                                             */
/***************************************************************/
static int psshade_warned = 0;
static int FPsshade(func_info *info)
{
    char psbuff[256];
    char *s = psbuff;
    int i;
    size_t len = sizeof(psbuff);

    /* 1 or 3 args */
    if (Nargs != 1 && Nargs != 3) return E_2MANY_ARGS;

    for (i=0; i<Nargs; i++) {
        if (ARG(i).type != INT_TYPE) return E_BAD_TYPE;
        if (ARG(i).v.val < 0) return E_2LOW;
        if (ARG(i).v.val > 100) return E_2HIGH;
    }

    if (!psshade_warned) {
        psshade_warned = 1;
        Wprint(tr("psshade() is deprecated; use SPECIAL SHADE instead."));
    }

    snprintf(s, len, "/_A LineWidth 2 div def ");
    len -= strlen(s);
    s += strlen(s);
    snprintf(s, len, "_A _A moveto ");
    len -= strlen(s);
    s += strlen(s);
    snprintf(s, len, "BoxWidth _A sub _A lineto BoxWidth _A sub BoxHeight _A sub lineto ");
    len -= strlen(s);
    s += strlen(s);
    if (Nargs == 1) {
        snprintf(s, len, "_A BoxHeight _A sub lineto closepath %d 100 div setgray fill 0.0 setgray", ARGV(0));
    } else {
        snprintf(s, len, "_A BoxHeight _A sub lineto closepath %d 100 div %d 100 div %d 100 div setrgbcolor fill 0.0 setgray", ARGV(0), ARGV(1), ARGV(2));
    }
    return RetStrVal(psbuff, info);
}

/***************************************************************/
/*                                                             */
/*  FPsmoon                                                    */
/*                                                             */
/*  Canned PostScript code for generating moon phases          */
/*                                                             */
/***************************************************************/
static int psmoon_warned = 0;

static int FPsmoon(func_info *info)
{
    char psbuff[512];
    char sizebuf[30];
    char fontsizebuf[30];
    char *s = psbuff;
    char const *extra = NULL;
    int size = -1;
    int fontsize = -1;
    size_t len = sizeof(psbuff);

    ASSERT_TYPE(0, INT_TYPE);
    if (ARGV(0) < 0) return E_2LOW;
    if (ARGV(0) > 3) return E_2HIGH;
    if (Nargs > 1) {
        ASSERT_TYPE(1, INT_TYPE);
        if (ARGV(1) < -1) return E_2LOW;
        size = ARGV(1);
        if (Nargs > 2) {
            ASSERT_TYPE(2, STR_TYPE);
            extra = ARGSTR(2);
            if (Nargs > 3) {
                ASSERT_TYPE(3, INT_TYPE);
                if (ARGV(3) <= 0) return E_2LOW;
                fontsize = ARGV(3);
            }
        }
    }
    if (!psmoon_warned) {
        psmoon_warned = 1;
        Wprint(tr("psmoon() is deprecated; use SPECIAL MOON instead."));
    }
    if (size > 0) {
        snprintf(sizebuf, sizeof(sizebuf), "%d", size);
    } else {
        strcpy(sizebuf, "DaySize 2 div");
    }

    if (fontsize > 0) {
        snprintf(fontsizebuf, sizeof(fontsizebuf), "%d", fontsize);
    } else {
        strcpy(fontsizebuf, "EntrySize");
    }

    snprintf(s, len, "gsave 0 setgray newpath Border %s add BoxHeight Border sub %s sub",
            sizebuf, sizebuf);
    len -= strlen(s);
    s += strlen(s);
    snprintf(s, len, " %s 0 360 arc closepath", sizebuf);
    len -= strlen(s);
    s += strlen(s);
    switch(ARGV(0)) {
    case 0:
        snprintf(s, len, " fill");
        len -= strlen(s);
        s += strlen(s);
        break;

    case 2:
        snprintf(s, len, " stroke");
        len -= strlen(s);
        s += strlen(s);
        break;

    case 1:
        snprintf(s, len, " stroke");
        len -= strlen(s);
        s += strlen(s);
        snprintf(s, len, " newpath Border %s add BoxHeight Border sub %s sub",
                sizebuf, sizebuf);
        len -= strlen(s);
        s += strlen(s);
        snprintf(s, len, " %s 90 270 arc closepath fill", sizebuf);
        len -= strlen(s);
        s += strlen(s);
        break;

    default:
        snprintf(s, len, " stroke");
        len -= strlen(s);
        s += strlen(s);
        snprintf(s, len, " newpath Border %s add BoxHeight Border sub %s sub",
                sizebuf, sizebuf);
        len -= strlen(s);
        s += strlen(s);
        snprintf(s, len, " %s 270 90 arc closepath fill", sizebuf);
        len -= strlen(s);
        s += strlen(s);
        break;
    }
    if (extra) {
        snprintf(s, len, " Border %s add %s add Border add BoxHeight border sub %s sub %s sub moveto /EntryFont findfont %s scalefont setfont (%s) show",
                sizebuf, sizebuf, sizebuf, sizebuf, fontsizebuf, extra);
        len -= strlen(s);
        s += strlen(s);
    }

    snprintf(s, len, " grestore");
    return RetStrVal(psbuff, info);
}

/***************************************************************/
/*                                                             */
/*  FMoonphase                                                 */
/*                                                             */
/*  Phase of moon for specified date/time.                     */
/*                                                             */
/***************************************************************/
static int FMoonphase(func_info *info)
{
    int date, time;

    switch(Nargs) {
    case 0:
        date = DSEToday;
        time = 0;
        break;
    case 1:
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        date = DATEPART(ARG(0));
        if (HASTIME(ARG(0))) {
            time = TIMEPART(ARG(0));
        } else {
            time = 0;
        }
        break;
    case 2:
        if (ARG(0).type == DATETIME_TYPE) return E_2MANY_ARGS;
        if (ARG(0).type != DATE_TYPE && ARG(1).type != TIME_TYPE) return E_BAD_TYPE;
        date = ARGV(0);
        time = ARGV(1);
        break;

    default: return E_SWERR;
    }

    RetVal.type = INT_TYPE;
    RETVAL = MoonPhase(date, time);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  FMoondate                                                  */
/*                                                             */
/*  Hunt for next occurrence of specified moon phase           */
/*                                                             */
/***************************************************************/
static int MoonStuff (int want_time, func_info *info);
static int FMoondate(func_info *info)
{
    return MoonStuff(DATE_TYPE, info);
}

static int FMoontime(func_info *info)
{
    return MoonStuff(TIME_TYPE, info);
}

static int FMoondatetime(func_info *info)
{
    return MoonStuff(DATETIME_TYPE, info);
}

static int FMoonrise(func_info *info)
{
    int start = DSEToday;
    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        start = DATEPART(ARG(0));
    }
    RetVal.type = DATETIME_TYPE;
    RETVAL = GetMoonrise(start);
    return OK;
}

static int FMoonset(func_info *info)
{
    int start = DSEToday;
    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        start = DATEPART(ARG(0));
    }
    RetVal.type = DATETIME_TYPE;
    RETVAL = GetMoonset(start);
    return OK;
}

static int FMoonrisedir(func_info *info)
{
    int start = DSEToday;
    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        start = DATEPART(ARG(0));
    }
    RetVal.type = INT_TYPE;
    RETVAL = GetMoonrise_angle(start);
    return OK;
}

static int FMoonsetdir(func_info *info)
{
    int start = DSEToday;
    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        start = DATEPART(ARG(0));
    }
    RetVal.type = INT_TYPE;
    RETVAL = GetMoonset_angle(start);
    return OK;
}

static int MoonStuff(int type_wanted, func_info *info)
{
    int startdate, starttim;
    int d, t;

    startdate = DSEToday;
    starttim = 0;

    ASSERT_TYPE(0, INT_TYPE);
    if (ARGV(0) < 0) return E_2LOW;
    if (ARGV(0) > 3) return E_2HIGH;
    if (Nargs >= 2) {
        if (!HASDATE(ARG(1))) return E_BAD_TYPE;
        startdate = DATEPART(ARG(1));
        if (HASTIME(ARG(1))) {
                starttim = TIMEPART(ARG(1));
        }

        if (Nargs >= 3) {
            if (HASTIME(ARG(1))) return E_2MANY_ARGS;
            ASSERT_TYPE(2, TIME_TYPE);
            starttim = ARGV(2);
        }
    }

    HuntPhase(startdate, starttim, ARGV(0), &d, &t);
    RetVal.type = type_wanted;
    switch(type_wanted) {
    case TIME_TYPE:
        RETVAL = t;
        break;
    case DATE_TYPE:
        RETVAL = d;
        break;
    case DATETIME_TYPE:
        RETVAL = d * MINUTES_PER_DAY + t;
        break;
    default:
        return E_BAD_TYPE;
    }
    return OK;
}

static int FTimepart(func_info *info)
{
    if (!HASTIME(ARG(0))) return E_BAD_TYPE;
    RetVal.type = TIME_TYPE;
    RETVAL = TIMEPART(ARG(0));
    return OK;
}

static int FDatepart(func_info *info)
{
    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    RetVal.type = DATE_TYPE;
    RETVAL = DATEPART(ARG(0));
    return OK;
}

#ifndef HAVE_SETENV
/* This is NOT a general-purpose replacement for setenv.  It's only
 * used for the timezone stuff! */
static int setenv(char const *varname, char const *val, int overwrite)
{
    static char tzbuf[128];
    if (strcmp(varname, "TZ")) {
        fprintf(ErrFp, "built-in setenv can only be used with TZ\n");
        abort();
    }
    if (!overwrite) {
        fprintf(ErrFp, "built-in setenv must have overwrite=1\n");
        abort();
    }

    if (strlen(val) > 250) {
        return -1;
    }
    snprintf(tzbuf, sizeof(tzbuf), "%s=%s", varname, val);
    return(putenv(tzbuf));
}
#endif
#ifndef HAVE_UNSETENV
/* This is NOT a general-purpose replacement for unsetenv.  It's only
 * used for the timezone stuff! */
static void unsetenv(char const *varname)
{
    static char tzbuf[128];
    if (strcmp(varname, "TZ")) {
        fprintf(ErrFp, "built-in unsetenv can only be used with TZ\n");
        abort();
    }
    snprintf(tzbuf, sizeof(tzbuf), "%s", varname);
    putenv(tzbuf);
}
#endif

/***************************************************************/
/*                                                             */
/*  FTz                                                        */
/*                                                             */
/*  Conversion between different timezones.                    */
/*                                                             */
/***************************************************************/
static int tz_set_tz(char const *tz)
{
    int r;
    if (tz == NULL) {
       unsetenv("TZ");
       r = 0;
    } else {
        r = setenv("TZ", tz, 1);
    }
    tzset();
    return r;
}

static int tz_convert(int year, int month, int day,
                      int hour, int minute,
                      char const *src_tz, char const *tgt_tz,
                      struct tm *tm)
{
    int r;
    time_t t;
    struct tm *res;
    char const *old_tz;

    /* init tm struct */
    tm->tm_sec = 0;
    tm->tm_min = minute;
    tm->tm_hour = hour;
    tm->tm_mday = day;
    tm->tm_mon = month;
    tm->tm_year = year - 1900;
    tm->tm_wday = 0; /* ignored by mktime */
    tm->tm_yday = 0; /* ignored by mktime */
    tm->tm_isdst = -1;  /* information not available */

    /* backup old TZ env var */
    old_tz = getenv("TZ");
    if (old_tz) {
        old_tz = StrDup(old_tz);
        if (!old_tz) return E_NO_MEM;
    }
    if (tgt_tz == NULL) {
        tgt_tz = old_tz;
    }

    /* set source TZ */
    r = tz_set_tz(src_tz);
    if (r == -1) {
        tz_set_tz(old_tz);
        if (old_tz) free((void *) old_tz);
        return -1;
    }

    /* create timestamp in UTC */
    t = mktime(tm);

    if (t == (time_t) -1) {
        tz_set_tz(old_tz);
        if (old_tz) free((void *) old_tz);
        return -1;
    }

    /* set target TZ */
    r = tz_set_tz(tgt_tz);
    if (r == -1) {
        tz_set_tz(old_tz);
        if (old_tz) free((void *) old_tz);
        return -1;
    }

    /* convert to target TZ */
    res = localtime_r(&t, tm);

    /* restore old TZ */
    tz_set_tz(old_tz);
    if (old_tz) free((void *) old_tz);

    /* return result */
    if (res == NULL) {
        return -1;
    } else {
        return 1;
    }
}

static int FTzconvert(func_info *info)
{
    int year, month, day, hour, minute, r;
    int dse, tim;
    struct tm tm;

    if (ARG(0).type != DATETIME_TYPE ||
        ARG(1).type != STR_TYPE) return E_BAD_TYPE;
    if (Nargs == 3 && ARG(2).type != STR_TYPE) return E_BAD_TYPE;

    FromDSE(DATEPART(ARG(0)), &year, &month, &day);

    r = TIMEPART(ARG(0));
    hour = r / 60;
    minute = r % 60;

    if (Nargs == 2) {
        r = tz_convert(year, month, day, hour, minute,
                       ARGSTR(1), NULL, &tm);
    } else {
        r = tz_convert(year, month, day, hour, minute,
                       ARGSTR(1), ARGSTR(2), &tm);
    }

    if (r == -1) return E_CANT_CONVERT_TZ;

    dse = DSE(tm.tm_year + 1900, tm.tm_mon, tm.tm_mday);
    tim = tm.tm_hour * 60 + tm.tm_min;
    RetVal.type = DATETIME_TYPE;
    RETVAL = dse * MINUTES_PER_DAY + tim;
    return OK;
}

static int
FSlide(func_info *info)
{
    int r, omit, d, i, localomit, amt;
    Token tok;
    int step = 1;
    int localargs = 2;

    if (!HASDATE(ARG(0))) return E_BAD_TYPE;
    ASSERT_TYPE(1, INT_TYPE);

    d = DATEPART(ARG(0));
    amt = ARGV(1);
    if (amt > 1000000) return E_2HIGH;
    if (amt < -1000000) return E_2LOW;

    if (Nargs > 2 && ARG(2).type == INT_TYPE) {
        step = ARGV(2);
        if (step < 1) return E_2LOW;
        localargs++;
    }
    localomit = 0;
    for (i=localargs; i<Nargs; i++) {
        if (ARG(i).type != STR_TYPE) return E_BAD_TYPE;
        FindToken(ARG(i).v.str, &tok);
        if (tok.type != T_WkDay) return E_UNKNOWN_TOKEN;
        localomit |= (1 << tok.val);
    }

    /* If ALL weekdays are omitted... barf! */
    if ((WeekdayOmits | localomit) == 0x7F && amt != 0) return E_2MANY_LOCALOMIT;
    if (amt > 0) {
        while(amt) {
            d += step;
            r = IsOmitted(d, localomit, NULL, &omit);
            if (r) return r;
            if (!omit) amt--;
        }
    } else {
        while(amt) {
            d -= step;
            if (d < 0) return E_DATE_OVER;
            r = IsOmitted(d, localomit, NULL, &omit);
            if (r) return r;
            if (!omit) amt++;
        }
    }
    RetVal.type = DATE_TYPE;
    RETVAL = d;
    return OK;
}

static int
FNonomitted(func_info *info)
{
    int d1, d2, ans, localomit, i;
    int omit, r;
    int step = 1;
    int localargs = 2;
    Token tok;

    if (!HASDATE(ARG(0)) ||
        !HASDATE(ARG(1))) {
        return E_BAD_TYPE;
    }
    d1 = DATEPART(ARG(0));
    d2 = DATEPART(ARG(1));
    if (d2 < d1) {
        i = d1;
        d1 = d2;
        d2 = i;
    }

    /* Check for a "step" argument - it's an INT */
    if (Nargs > 2 && ARG(2).type == INT_TYPE) {
        step = ARGV(2);
        if (step < 1) return E_2LOW;
        localargs++;
    }
    localomit = 0;
    for (i=localargs; i<Nargs; i++) {
        if (ARG(i).type != STR_TYPE) return E_BAD_TYPE;
        FindToken(ARG(i).v.str, &tok);
        if (tok.type != T_WkDay) return E_UNKNOWN_TOKEN;
        localomit |= (1 << tok.val);
    }

    ans = 0;
    while (d1 < d2) {
        r = IsOmitted(d1, localomit, NULL, &omit);
        if (r) return r;
        if (!omit) {
            ans++;
        }
        d1 += step;
    }
    RetVal.type = INT_TYPE;
    RETVAL = ans;
    return OK;
}

static int
FWeekno(func_info *info)
{
    int dse = DSEToday;
    int wkstart = 0; /* Week start on Monday */
    int daystart = 29; /* First week starts on wkstart on or after Dec. 29 */
    int monstart;
    int candidate;

    int y, m, d;

    if (Nargs >= 1) {
        if (!HASDATE(ARG(0))) return E_BAD_TYPE;
        dse = DATEPART(ARG(0));
    }
    if (Nargs >= 2) {
        ASSERT_TYPE(1, INT_TYPE);
        if (ARGV(1) < 0) return E_2LOW;
        if (ARGV(1) > 6) return E_2HIGH;
        wkstart = ARGV(1);
        /* Convert 0=Sun to 0=Mon */
        wkstart--;
        if (wkstart < 0) wkstart = 6;
        if (Nargs >= 3) {
            ASSERT_TYPE(2, INT_TYPE);
            if (ARGV(2) < 1) return E_2LOW;
            if (ARGV(2) > 31) return E_2HIGH;
            daystart = ARGV(2);
        }
    }

    RetVal.type = INT_TYPE;
    /* If start day is 7, first week starts after Jan,
       otherwise after Dec. */
    if (daystart <= 7) {
        monstart = 0;
    } else {
        monstart = 11;
    }

    FromDSE(dse, &y, &m, &d);

    /* Try this year */
    candidate = DSE(y, monstart, daystart);
    while((candidate % 7) != wkstart) candidate++;

    if (candidate <= dse) {
        RETVAL = ((dse - candidate) / 7) + 1;
        return OK;
    }

    if (y-1 < BASE) return E_DATE_OVER;
    /* Must be last year */
    candidate = DSE(y-1, monstart, daystart);
    while((candidate % 7) != wkstart) candidate++;
    if (candidate <= dse) {
        RETVAL = ((dse - candidate) / 7) + 1;
        return OK;
    }

    if (y-2 < BASE) return E_DATE_OVER;
    /* Holy cow! */
    candidate = DSE(y-2, monstart, daystart);
    while((candidate % 7) != wkstart) candidate++;
    RETVAL = ((dse - candidate) / 7) + 1;
    return OK;
}

static int
FEval(func_info *info)
{
    expr_node *n;
    int r;

    ASSERT_TYPE(0, STR_TYPE);
    char const *e = ARGSTR(0);

    n = parse_expression(&e, &r, NULL);
    if (r != OK) {
        info->nonconst = 1;
        return r;
    }

    r = evaluate_expr_node(n, NULL, &(info->retval), &(info->nonconst));
    free_expr_tree(n);
    return r;
}

static int
FEvalTrig(func_info *info)
{
    Parser p;
    Trigger trig;
    TimeTrig tim;
    int dse, scanfrom;
    int r;

    ASSERT_TYPE(0, STR_TYPE);
    if (Nargs >= 2) {
        if (!HASDATE(ARG(1))) return E_BAD_TYPE;
        scanfrom = DATEPART(ARG(1));
    } else {
        scanfrom = NO_DATE;
    }

    CreateParser(ARGSTR(0), &p);
    p.allownested = 0;
    r = ParseRem(&p, &trig, &tim);
    if (r) {
        DestroyParser(&p);
        return r;
    }
    if (trig.typ != NO_TYPE) {
        DestroyParser(&p);
        FreeTrig(&trig);
        return E_PARSE_ERR;
    }
    if (scanfrom == NO_DATE) {
        dse = ComputeTrigger(trig.scanfrom, &trig, &tim, &r, 0);
    } else {
        /* Hokey... */
        if (trig.scanfrom != DSEToday) {
            Wprint(tr("Warning: SCANFROM is ignored in two-argument form of evaltrig()"));
        }
        dse = ComputeTrigger(scanfrom, &trig, &tim, &r, 0);
    }
    if (r == E_CANT_TRIG && trig.maybe_uncomputable) {
        r = 0;
        dse = -1;
    }
    FreeTrig(&trig);
    DestroyParser(&p);
    if (r) return r;
    if (dse < 0) {
        RetVal.type = INT_TYPE;
        RETVAL = dse;
    } else if (tim.ttime == NO_TIME) {
        RetVal.type = DATE_TYPE;
        RETVAL = dse;
    } else {
        RetVal.type = DATETIME_TYPE;
        RETVAL = (MINUTES_PER_DAY * dse) + tim.ttime;
    }
    return OK;
}

static int
FMultiTrig(func_info *info)
{
    Parser p;
    Trigger trig;
    TimeTrig tim;
    int dse;
    int r;
    int i;
    int earliest = -1;

    RetVal.type = DATE_TYPE;
    RETVAL = 0;

    for (i=0; i<Nargs; i++) {
        ASSERT_TYPE(i, STR_TYPE);
    }
    for (i=0; i<Nargs; i++) {
        CreateParser(ARGSTR(i), &p);
        p.allownested = 0;
        r = ParseRem(&p, &trig, &tim);
        if (r) {
            DestroyParser(&p);
            return r;
        }
        if (trig.typ != NO_TYPE) {
            DestroyParser(&p);
            FreeTrig(&trig);
            return E_PARSE_ERR;
        }
        if (tim.ttime != NO_TIME) {
            Eprint(tr("Cannot use AT clause in multitrig() function"));
            return E_PARSE_ERR;
        }
        dse = ComputeTrigger(trig.scanfrom, &trig, &tim, &r, 0);
        DestroyParser(&p);

        if (r != E_CANT_TRIG) {
            if (dse < earliest || earliest < 0) {
                earliest = dse;
            }
        }
        FreeTrig(&trig);
    }
    if (earliest >= 0) {
        RETVAL = earliest;
    }

    return OK;
}

static int LastTrig = 0;
static int
FTrig(func_info *info)
{
    Parser p;
    Trigger trig;
    TimeTrig tim;
    int dse;
    int r;
    int i;

    RetVal.type = DATE_TYPE;
    if (Nargs == 0) {
        RETVAL = LastTrig;
        return OK;
    }

    for (i=0; i<Nargs; i++) {
        ASSERT_TYPE(i, STR_TYPE);
    }

    RETVAL = 0;

    for (i=0; i<Nargs; i++) {
        CreateParser(ARGSTR(i), &p);
        p.allownested = 0;
        r = ParseRem(&p, &trig, &tim);
        if (r) {
            DestroyParser(&p);
            return r;
        }
        if (trig.typ != NO_TYPE) {
            DestroyParser(&p);
            FreeTrig(&trig);
            return E_PARSE_ERR;
        }
        dse = ComputeTrigger(trig.scanfrom, &trig, &tim, &r, 0);
        DestroyParser(&p);

        if (r == E_CANT_TRIG) {
            FreeTrig(&trig);
            continue;
        }
        if (ShouldTriggerReminder(&trig, &tim, dse, &r)) {
            LastTrig = dse;
            RETVAL = dse;
            FreeTrig(&trig);
            return OK;
        }
        FreeTrig(&trig);
    }
    return OK;
}

static int
rows_or_cols(func_info *info, int want_rows)
{
    struct winsize w;
    int fd = STDOUT_FILENO;

    int opened = 0;

    RetVal.type = INT_TYPE;
    if (!isatty(fd)) {
        fd = open("/dev/tty", O_RDONLY);
        if (fd < 0) {
            RETVAL = -1;
            return OK;
        }
        opened = 1;
    }
    if (ioctl(fd, TIOCGWINSZ, &w) == 0) {
        if (want_rows) RETVAL = w.ws_row;
        else           RETVAL = w.ws_col;
    } else {
        RETVAL = -1;
    }
    if (opened) {
        close(fd);
    }
    return OK;
}

static int FRows(func_info *info)
{
    return rows_or_cols(info, 1);
}
static int FColumns(func_info *info)
{
#ifdef REM_USE_WCHAR
    size_t len;
    wchar_t *buf, *s;
    int width;
#endif
    if (Nargs == 0) {
        return rows_or_cols(info, 0);
    }
    ASSERT_TYPE(0, STR_TYPE);
#ifdef REM_USE_WCHAR
    len = mbstowcs(NULL, ARGSTR(0), 0);
    if (len == (size_t) -1) return E_NO_MEM;
    buf = calloc(len+1, sizeof(wchar_t));
    if (!buf) return E_NO_MEM;
    (void) mbstowcs(buf, ARGSTR(0), len+1);

    s = buf;
    width = 0;
    while (*s) {
        if (*s == 0x1B && *(s+1) == '[') {
            /* Skip escape sequences */
            s += 2;
            while (*s && (*s < 0x40 || *s > 0x7E)) {
                s++;
            }
            if (*s) {
                s++;
            }
            continue;
        }
        width += wcwidth(*s);
        s++;
    }
    free(buf);
    RetVal.type = INT_TYPE;
    RETVAL = width;
    return OK;
#else
    return E_BAD_TYPE;
#endif
}

/* The following sets of functions are for computing solstices and equinoxes.
   They are based on the algorithms described in "Astronomical Algorithms",
   second edition, by Jean Meeus.  ISBN 0-943396-61-1 */

/* The following are taken from Astronomical Algorithms, 2nd ed., page 178 */
static double
mean_march_equinox(double y)
{
    return 2451623.80984 + 365242.37404*y + 0.05169*y*y - 0.00411*y*y*y - 0.00057*y*y*y*y;
}

static double
mean_june_solstice(double y)
{
    return 2451716.56767 + 365241.62603*y + 0.00325*y*y + 0.00888*y*y*y - 0.00030*y*y*y*y;
}

static double
mean_september_equinox(double y)
{
    return 2451810.21715 + 365242.01767*y - 0.11575*y*y + 0.00337*y*y*y + 0.00078*y*y*y*y;
}

static double
mean_december_solstice(double y)
{
    return 2451900.05952 + 365242.74049*y - 0.06223*y*y - 0.00823*y*y*y + 0.00032*y*y*y*y;
}

/* Cosine of an angle specified in degrees */
#define PI_BY_180 0.01745329251994329576923690768
#define cosd(theta) cos( (theta) * PI_BY_180)

/* Astronomical Algorithms by Meeus, p. 179
   These weird periodic components refine the mean solstice/equinox dates
   calculated with the simpler degree-4 polynomials above */
static double
meeus_periodic_components(double t)
{
    return
        485 * cosd(324.96 +   1934.136 * t) +
        203 * cosd(337.23 +  32964.467 * t) +
        199 * cosd(342.08 +     20.186 * t) +
        182 * cosd( 27.85 + 445267.112 * t) +
        156 * cosd( 73.14 +  45036.886 * t) +
        136 * cosd(171.52 +  22518.443 * t) +
         77 * cosd(222.54 +  65928.934 * t) +
         74 * cosd(296.72 +   3034.906 * t) +
         70 * cosd(243.58 +   9037.513 * t) +
         58 * cosd(119.81 +  33718.147 * t) +
         52 * cosd(297.17 +    150.678 * t) +
         50 * cosd( 21.02 +   2281.226 * t) +
         45 * cosd(247.54 +  29929.562 * t) +
         44 * cosd(325.15 +  31555.956 * t) +
         29 * cosd( 60.93 +   4443.417 * t) +
         18 * cosd(155.12 +  67555.328 * t) +
         17 * cosd(288.79 +   4562.452 * t) +
         16 * cosd(198.04 +  62894.029 * t) +
         14 * cosd(199.76 +  31436.921 * t) +
         12 * cosd( 95.39 +  14577.848 * t) +
         12 * cosd(287.11 +  31931.756 * t) +
         12 * cosd(320.81 +  34777.259 * t) +
          9 * cosd(227.73 +   1222.114 * t) +
          8 * cosd( 15.45 +  16859.074 * t);
}

static double
julian_solstice_equinox(int y, int which)
{
    double jde0;
    double dy;
    double t, w, dlambda, s;

    dy = ((double) y - 2000.0) / 1000.0;
    switch(which) {
    case 0:
        jde0 = mean_march_equinox(dy);
        break;
    case 1:
        jde0 = mean_june_solstice(dy);
        break;
    case 2:
        jde0 = mean_september_equinox(dy);
        break;
    case 3:
        jde0 = mean_december_solstice(dy);
        break;
    default:
        return -1.0;
    }

    t = (jde0 - 2451545.0) / 36525.0;
    w = 35999.373 * t - 2.47;
    dlambda = 1 + 0.0334 * cosd(w) + 0.0007 * cosd(2*w);
    s = meeus_periodic_components(t);

    return jde0 + (0.00001 * s) / dlambda;
}

/* Returns a value suitable for a datetime object.  Assumes that BASE = 1990*/
static int
solstice_equinox_for_year(int y, int which)
{
    double j = julian_solstice_equinox(y, which);

    if (j < 0) {
        return -1;
    }

    j -= 2447892.50000;  /* This is the Julian date of midnight, 1 Jan 1990 UTC */
    int dse = (int) j;

    int min = (int) floor((j - (double) dse) * MINUTES_PER_DAY);
    int ret;

    /* Convert from UTC to local time */
    if (UTCToLocalHelper(dse * MINUTES_PER_DAY + min, &ret) != OK) {
        return -1;
    }
    return ret;
}

/* Solstice / equinox function */
static int
FSoleq(func_info *info)
{
    int y, dse, which, ret;

    RetVal.type = ERR_TYPE;

    dse = NO_DATE;
    ASSERT_TYPE(0, INT_TYPE);
    which = ARGV(0);
    if (which < 0) {
        return E_2LOW;
    } else if (which > 3) {
        return E_2HIGH;
    }

    if (Nargs > 1) {
        if (ARG(1).type == INT_TYPE) {
            y = ARGV(1);
            if (y < BASE) {
                return E_2LOW;
            } else if (y > BASE+YR_RANGE) {
                return E_2HIGH;
            }
        } else if (HASDATE(ARG(1))) {
            dse = DATEPART(ARG(1));
            FromDSE(dse, &y, NULL, NULL);  /* We just want the year */
        } else {
            return E_BAD_TYPE;
        }
    } else {
        /* If no second argument, default to today */
        dse = DSEToday;
        FromDSE(dse, &y, NULL, NULL);  /* We just want the year */
    }

    ret = solstice_equinox_for_year(y, which);
    if (ret < 0) return E_MKTIME_PROBLEM;
    if (dse != NO_DATE && (ret / MINUTES_PER_DAY) < dse) {
        ret = solstice_equinox_for_year(y+1, which);
        if (ret < 0) return E_MKTIME_PROBLEM;
    }
    RetVal.type = DATETIME_TYPE;
    RETVAL = ret;
    return OK;
}

/* Compare two strings case-insensitively, where we KNOW
   that the second string is definitely lower-case */
static int strcmp_lcfirst(char const *s1, char const *s2)
{
    int r;
    while (*s1 && *s2) {
        r = tolower(*s1) - *s2;
        if (r) return r;
        s1++;
        s2++;
    }
    return tolower(*s1) - *s2;
}

/***************************************************************/
/*                                                             */
/*  FindBuiltinFunc                                            */
/*                                                             */
/*  Find a built-in function.                                  */
/*                                                             */
/***************************************************************/
BuiltinFunc *FindBuiltinFunc(char const *name)
{
    int top=NumFuncs-1, bot=0;
    int mid, r;
    while (top >= bot) {
        mid = (top + bot) / 2;
        r = strcmp_lcfirst(name, Func[mid].name);
        if (!r) return &Func[mid];
        else if (r > 0) bot = mid+1;
        else top = mid-1;
    }
    return NULL;
}

void
print_builtinfunc_tokens(void)
{
    int i;
    printf("\n# Built-in Functions\n\n");
    for (i=0; i<NumFuncs; i++) {
        printf("%s\n", Func[i].name);
    }
}
