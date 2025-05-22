/***************************************************************/
/*                                                             */
/*  VAR.C                                                      */
/*                                                             */
/*  This file contains routines, structures, etc for           */
/*  user- and system-defined variables.                        */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <stddef.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <locale.h>
#include "types.h"
#include "globals.h"
#include "protos.h"
#include "err.h"
#define UPPER(c) (islower(c) ? toupper(c) : c)

#define VARIABLE GetErr(E_VAR)
#define VALUE    GetErr(E_VAL)
#define UNDEF    GetErr(E_UNDEF)

static int IntMin = INT_MIN;
static int IntMax = INT_MAX;

static hash_table VHashTbl;
static int SetSysVarHelper(SysVar *v, Value *value);

static unsigned int VarHashFunc(void *x)
{
    Var *v = (Var *) x;
    return HashVal_ignorecase(v->name);
}

static int VarCompareFunc(void *a, void *b)
{
    Var *x = (Var *) a;
    Var *y = (Var *) b;
    return StrCmpi(x->name, y->name);
}

void
InitVars(void)
{
    if (hash_table_init(&VHashTbl, offsetof(Var, link),
                        VarHashFunc, VarCompareFunc) < 0) {
        fprintf(ErrFp, "Unable to initialize variable hash table: Out of memory.  Exiting.\n");
        exit(1);
    }
}

static double
strtod_in_c_locale(char const *str, char **endptr)
{
    /* Get current locale */
    char const *loc = setlocale(LC_NUMERIC, NULL);
    double x;

    /* If it failed, punt */
    if (!loc) {
        return strtod(str, endptr);
    }

    /* Set locale to C */
    setlocale(LC_NUMERIC, "C");

    x = strtod(str, endptr);

    /* Back to original locale */
    setlocale(LC_NUMERIC, loc);

    /* If we got an error, try in original locale, but issue a warning */
    if (**endptr) {
        x = strtod(str, endptr);
        if (!**endptr) {
            Wprint(tr("Accepting \"%s\" for $Latitude/$Longitude, but you should use the \"C\" locale decimal separator \".\" instead"), str);
        }
    }
    return x;
}

static void deprecated_var(char const *var, char const *instead)
{
    if (DebugFlag & DB_PRTLINE) {
        Wprint(tr("%s is deprecated; use %s instead"), var, instead);
    }
}

static int latlong_component_func(int do_set, Value *val, int *var, int min, int max, char const *varname, char const *newvarname)
{
    if (!do_set) {
        val->type = INT_TYPE;
        val->v.val = *var;
        return OK;
    }
    deprecated_var(varname, newvarname);
    if (val->type != INT_TYPE) return E_BAD_TYPE;
    if (val->v.val < min) return E_2LOW;
    if (val->v.val > max) return E_2HIGH;
    *var = val->v.val;
    set_lat_and_long_from_components();
    return OK;
}
static int latdeg_func(int do_set, Value *val)
{
    return latlong_component_func(do_set, val, &LatDeg, -90, 90, "$LatDeg", "$Latitude");
}

static int latmin_func(int do_set, Value *val)
{
    return latlong_component_func(do_set, val, &LatMin, -59, 59, "$LatMin", "$Latitude");
}

static int latsec_func(int do_set, Value *val)
{
    return latlong_component_func(do_set, val, &LatSec, -59, 59, "$LatSec", "$Latitude");
}

static int longdeg_func(int do_set, Value *val)
{
    return latlong_component_func(do_set, val, &LongDeg, -180, 180, "$LongDeg", "$Longitude");
}

static int longmin_func(int do_set, Value *val)
{
    return latlong_component_func(do_set, val, &LongMin, -59, 59, "$LongMin", "$Longitude");
}

static int longsec_func(int do_set, Value *val)
{
    return latlong_component_func(do_set, val, &LongSec, -59, 59, "$LongSec", "$Longitude");
}

static int latitude_longitude_func(int do_set, Value *val, double *var, double min, double max) {
    char buf[64];
    double x;
    char *endptr;
    char const *loc = setlocale(LC_NUMERIC, NULL);

    if (!do_set) {
        if (loc) {
            setlocale(LC_NUMERIC, "C");
        }
        snprintf(buf, sizeof(buf), "%f", *var);
        if (loc) {
            setlocale(LC_NUMERIC, loc);
        }
        val->v.str = malloc(strlen(buf)+1);
        if (!val->v.str) return E_NO_MEM;
        strcpy(val->v.str, buf);
        val->type = STR_TYPE;
        return OK;
    }
    if (val->type == INT_TYPE) {
        x = (double) val->v.val;
    } else {
        if (val->type != STR_TYPE) return E_BAD_TYPE;
        x = strtod_in_c_locale(val->v.str, &endptr);
        if (*endptr) return E_BAD_TYPE;
    }
    if (x < min) return E_2LOW;
    if (x > max) return E_2HIGH;
    *var = x;
    set_components_from_lat_and_long();
    return OK;
}

static int longitude_func(int do_set, Value *val)
{
    return latitude_longitude_func(do_set, val, &Longitude, -180.0, 180.0);
}

static int latitude_func(int do_set, Value *val)
{
    return latitude_longitude_func(do_set, val, &Latitude, -90.0, 90.0);
}

static int oncefile_func(int do_set, Value *val)
{
    if (do_set) {
        if (val->type != STR_TYPE) return E_BAD_TYPE;
        if (! (*val->v.str) && (!OnceFile || !*OnceFile)) {
            /* Trying to set already-empty string to empty string */
            return OK;
        }
        if (OnceFile && !strcmp(OnceFile, val->v.str)) {
            /* Trying to set to the exact same value */
            return OK;
        }

        if (ProcessedOnce) {
            Wprint(tr("Not setting $OnceFile: Already processed a reminder with a ONCE clause"));
            return OK;
        }
        if (OnceFile) {
            free( (void *) OnceFile);
        }
        OnceFile = StrDup(val->v.str);
        if (!OnceFile) return E_NO_MEM;
        return OK;
    }
    if (!OnceFile) {
        val->v.str = StrDup("");
    } else {
        val->v.str = StrDup(OnceFile);
    }
    if (!val->v.str) return E_NO_MEM;
    val->type = STR_TYPE;
    return OK;

}
static int terminal_bg_func(int do_set, Value *val)
{
    UNUSED(do_set);
    val->type = INT_TYPE;
    val->v.val = GetTerminalBackground();
    return OK;
}

static int trig_time_func(int do_set, Value *val)
{
    UNUSED(do_set);
    if (LastTriggerTime != NO_TIME) {
        val->type = TIME_TYPE;
        val->v.val = LastTriggerTime;
    } else {
        val->type = INT_TYPE;
        val->v.val = 0;
    }
    return OK;
}

static int trig_date_func(int do_set, Value *val)
{
    UNUSED(do_set);
    if (!LastTrigValid) {
        val->type = INT_TYPE;
        val->v.val = 0;
    } else {
        val->type = DATE_TYPE;
        val->v.val = LastTriggerDate;
    }
    return OK;
}
static int trig_day_func(int do_set, Value *val)
{
    int y, m, d;
    UNUSED(do_set);
    val->type = INT_TYPE;
    if (!LastTrigValid) {
        val->v.val = -1;
        return OK;
    }

    FromDSE(LastTriggerDate, &y, &m, &d);
    val->v.val = d;
    return OK;
}

static int timet_is_64_func(int do_set, Value *val)
{
    UNUSED(do_set);
    val->type = INT_TYPE;
    if (sizeof(time_t) >= (64 / CHAR_BIT)) {
        val->v.val = 1;
    } else {
        val->v.val = 0;
    }
    return OK;
}

static int trig_mon_func(int do_set, Value *val)
{
    int y, m, d;
    UNUSED(do_set);
    val->type = INT_TYPE;
    if (!LastTrigValid) {
        val->v.val = -1;
        return OK;
    }

    FromDSE(LastTriggerDate, &y, &m, &d);
    val->v.val = m+1;
    return OK;
}

static int trig_year_func(int do_set, Value *val)
{
    int y, m, d;
    UNUSED(do_set);
    val->type = INT_TYPE;
    if (!LastTrigValid) {
        val->v.val = -1;
        return OK;
    }

    FromDSE(LastTriggerDate, &y, &m, &d);
    val->v.val = y;
    return OK;
}

static int trig_wday_func(int do_set, Value *val)
{
    val->type = INT_TYPE;
    UNUSED(do_set);
    if (!LastTrigValid) {
        val->v.val = -1;
        return OK;
    }

    val->v.val = (LastTriggerDate + 1) % 7;
    return OK;
}

static int today_date_func(int do_set, Value *val)
{
    UNUSED(do_set);
    val->type = DATE_TYPE;
    val->v.val = DSEToday;
    return OK;
}
static int today_day_func(int do_set, Value *val)
{
    int y, m, d;
    UNUSED(do_set);
    val->type = INT_TYPE;
    FromDSE(DSEToday, &y, &m, &d);
    val->v.val = d;
    return OK;
}

static int today_mon_func(int do_set, Value *val)
{
    int y, m, d;
    UNUSED(do_set);
    val->type = INT_TYPE;
    FromDSE(DSEToday, &y, &m, &d);
    val->v.val = m+1;
    return OK;
}

static int today_year_func(int do_set, Value *val)
{
    int y, m, d;
    UNUSED(do_set);
    val->type = INT_TYPE;
    FromDSE(DSEToday, &y, &m, &d);
    val->v.val = y;
    return OK;
}

static int today_wday_func(int do_set, Value *val)
{
    UNUSED(do_set);
    val->type = INT_TYPE;
    val->v.val = (DSEToday + 1) % 7;
    return OK;
}

static int datetime_sep_func(int do_set, Value *val)
{
    if (!do_set) {
        val->v.str = malloc(2);
        if (!val->v.str) return E_NO_MEM;
        val->v.str[0] = DateTimeSep;
        val->v.str[1] = 0;
        val->type = STR_TYPE;
        return OK;
    }
    if (val->type != STR_TYPE) return E_BAD_TYPE;
    if (strcmp(val->v.str, "T") &&
        strcmp(val->v.str, "@")) {
        return E_BAD_TYPE;
    }
    DateTimeSep = val->v.str[0];
    return OK;
}

static int expr_time_limit_func(int do_set, Value *val)
{
    if (!do_set) {
        val->type = INT_TYPE;
        val->v.val = ExpressionEvaluationTimeLimit;
        return OK;
    }
    if (val->type != INT_TYPE) return E_BAD_TYPE;
    if (val->v.val < 0) return E_2LOW;

    if (!TopLevel()) {
        /* Ignore attempts to set from non-toplevel unless it's
           lower than current value */
        if (val->v.val == 0 ||
            val->v.val >= ExpressionEvaluationTimeLimit) {
            return OK;
        }
    }

    ExpressionEvaluationTimeLimit = val->v.val;
    return OK;
}

static int default_color_func(int do_set, Value *val)
{
    int col_r, col_g, col_b;
    if (!do_set) {
    /* 12 = strlen("255 255 255\0") */
        val->v.str = malloc(12);
        if (!val->v.str) return E_NO_MEM;
        snprintf(val->v.str, 12, "%d %d %d",
                 DefaultColorR,
                 DefaultColorG,
                 DefaultColorB
            );
        val->type = STR_TYPE;
        return OK;
    }
    if (val->type != STR_TYPE) return E_BAD_TYPE;
    if (sscanf(val->v.str, "%d %d %d", &col_r, &col_g, &col_b) != 3) {
        return E_BAD_TYPE;
    }
    /* They either all have to be -1, or all between 0 and 255 */
    if (col_r == -1 && col_g == -1 && col_b == -1) {
        DefaultColorR = -1;
        DefaultColorG = -1;
        DefaultColorB = -1;
        return OK;
    }
    if (col_r < 0) return E_2LOW;
    if (col_r > 255) return E_2HIGH;
    if (col_g < 0) return E_2LOW;
    if (col_g > 255) return E_2HIGH;
    if (col_b < 0) return E_2LOW;
    if (col_b > 255) return E_2HIGH;

    DefaultColorR = col_r;
    DefaultColorG = col_g;
    DefaultColorB = col_b;
    return OK;
}

static int date_sep_func(int do_set, Value *val)
{
    if (!do_set) {
        val->v.str = malloc(2);
        if (!val->v.str) return E_NO_MEM;
        val->v.str[0] = DateSep;
        val->v.str[1] = 0;
        val->type = STR_TYPE;
        return OK;
    }
    if (val->type != STR_TYPE) return E_BAD_TYPE;
    if (strcmp(val->v.str, "/") &&
        strcmp(val->v.str, "-")) {
        return E_BAD_TYPE;
    }
    DateSep = val->v.str[0];
    return OK;
}

static int time_sep_func(int do_set, Value *val)
{
    if (!do_set) {
        val->v.str = malloc(2);
        if (!val->v.str) return E_NO_MEM;
        val->v.str[0] = TimeSep;
        val->v.str[1] = 0;
        val->type = STR_TYPE;
        return OK;
    }
    if (val->type != STR_TYPE) return E_BAD_TYPE;
    if (strcmp(val->v.str, ":") &&
        strcmp(val->v.str, ".")) {
        return E_BAD_TYPE;
    }
    TimeSep = val->v.str[0];
    return OK;
}

/***************************************************************/
/*                                                             */
/*  HashVal_ignorecase                                         */
/*  Given a string, compute the hash value case-insensitively  */
/*                                                             */
/***************************************************************/
unsigned int HashVal_ignorecase(char const *str)
{
    unsigned int h = 0, high;
    while(*str) {
        h = (h << 4) + (unsigned int) UPPER(*str);
        str++;
        high = h & 0xF0000000;
        if (high) {
            h ^= (high >> 24);
        }
        h &= ~high;
    }
    return h;
}

/***************************************************************/
/*                                                             */
/*  FindVar                                                    */
/*  Given a string, find the variable whose name is that       */
/*  string.  If create is 1, create the variable.              */
/*                                                             */
/***************************************************************/
Var *FindVar(char const *str, int create)
{
    Var *v;
    Var candidate;
    StrnCpy(candidate.name, str, VAR_NAME_LEN);

    v = (Var *) hash_table_find(&VHashTbl, &candidate);
    if (v != NULL || !create) return v;

    /* Create the variable */
    v = NEW(Var);
    if (!v) return v;
    v->v.type = INT_TYPE;
    v->v.v.val = 0;
    v->preserve = 0;
    StrnCpy(v->name, str, VAR_NAME_LEN);

    hash_table_insert(&VHashTbl, v);
    return v;
}

/***************************************************************/
/*                                                             */
/*  DeleteVar                                                  */
/*  Given a string, find the variable whose name is that       */
/*  string and delete it.                                      */
/*                                                             */
/***************************************************************/
int DeleteVar(char const *str)
{
    Var *v;

    v = FindVar(str, 0);
    if (!v) return E_NOSUCH_VAR;
    DestroyValue(v->v);
    hash_table_delete(&VHashTbl, v);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  SetVar                                                     */
/*                                                             */
/*  Set the indicated variable to the specified value.         */
/*                                                             */
/***************************************************************/
int SetVar(char const *str, Value const *val, int nonconst_expr)
{
    Var *v = FindVar(str, 1);

    if (!v) return E_NO_MEM;  /* Only way FindVar can fail */

    DestroyValue(v->v);
    v->v = *val;
    v->nonconstant = nonconst_expr;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  GetVarValue                                                */
/*                                                             */
/*  Get a copy of the value of the variable.                   */
/*                                                             */
/***************************************************************/
int GetVarValue(char const *str, Value *val)
{
    Var *v;

    v=FindVar(str, 0);

    if (!v) {
        Eprint("%s: `%s'", GetErr(E_NOSUCH_VAR), str);
        return E_NOSUCH_VAR;
    }
    return CopyValue(val, &v->v);
}

/***************************************************************/
/*                                                             */
/*  DoSet - set a variable.                                    */
/*                                                             */
/***************************************************************/
int DoSet (Parser *p)
{
    Value v;
    int r;
    int ch;
    int ignoring = should_ignore_line();
    DynamicBuffer buf;
    DynamicBuffer buf2;
    DBufInit(&buf);
    DBufInit(&buf2);
    Var *var;

    r = ParseIdentifier(p, &buf);
    if (r) return r;

    if (ignoring) {
        /* We're only here to mark a variable as non-const */
        if (in_constant_context()) {
            DBufFree(&buf);
            return OK;
        }
        var = FindVar(DBufValue(&buf), 0);
        if (var) {
            nonconst_debug(var->nonconstant, tr("Variable assignment considered non-constant because of context"));
            var->nonconstant = 1;
        }
        DBufFree(&buf);
        return OK;
    }

    /* Allow optional equals-sign:  SET var = value */
    ch = ParseNonSpaceChar(p, &r, 1);
    if (r) return r;
    if (ch == '=') {
        ParseNonSpaceChar(p, &r, 0);
        if (r) return r;
    }

    if (p->isnested) {
        Eprint("%s", tr("Do not use [] around expression in SET command"));
        return E_CANTNEST_FDEF;
    }

    p->nonconst_expr = 0;
    r = EvaluateExpr(p, &v);
    if (r) {
        DBufFree(&buf);
        return r;
    }

    r = ParseToken(p, &buf2);
    if (r) return r;
    if (DBufLen(&buf2)) {
        DBufFree(&buf2);
        return E_EXPECTING_EOL;
    }
    DBufFree(&buf2);
    if (*DBufValue(&buf) == '$') r = SetSysVar(DBufValue(&buf)+1, &v);
    else {
        if (p->nonconst_expr || !in_constant_context()) {
            r = SetVar(DBufValue(&buf), &v, 1);
        } else {
            r = SetVar(DBufValue(&buf), &v, 0);
        }
        if (DebugFlag & DB_NONCONST) {
            if (!in_constant_context() && !p->nonconst_expr) {
                Wprint(tr("Variable assignment considered non-constant because of context"));
            }
        }
    }
    if (buf.len > VAR_NAME_LEN) {
        Wprint(tr("Warning: Variable name `%.*s...' truncated to `%.*s'"),
               VAR_NAME_LEN, DBufValue(&buf), VAR_NAME_LEN, DBufValue(&buf));
    }
    DBufFree(&buf);
    return r;
}

/***************************************************************/
/*                                                             */
/*  DoUnset - delete a bunch of variables.                     */
/*                                                             */
/***************************************************************/
int DoUnset (Parser *p)
{
    int r;

    DynamicBuffer buf;
    DBufInit(&buf);

    r = ParseToken(p, &buf);
    if (r) return r;
    if (!DBufLen(&buf)) {
        DBufFree(&buf);
        return E_EOLN;
    }

    (void) DeleteVar(DBufValue(&buf));  /* Ignore error - nosuchvar */

/* Keep going... */
    while(1) {
        r = ParseToken(p, &buf);
        if (r) return r;
        if (!DBufLen(&buf)) {
            DBufFree(&buf);
            return OK;
        }
        (void) DeleteVar(DBufValue(&buf));
    }
}

/***************************************************************/
/*                                                             */
/*  DoDump                                                     */
/*                                                             */
/*  Command file command to dump variable table.               */
/*                                                             */
/***************************************************************/
int DoDump(ParsePtr p)
{
    int r;
    Var *v;
    DynamicBuffer buf;
    int dump_constness = 0;

    if (PurgeMode) return OK;

    DBufInit(&buf);
    r = ParseToken(p, &buf);
    if (r) return r;
    if (!strcmp(DBufValue(&buf), "-c")) {
        dump_constness = 1;
        DBufFree(&buf);
        DBufInit(&buf);
        r = ParseToken(p, &buf);
        if (r) return r;
    }
    if (!*DBufValue(&buf) ||
        *DBufValue(&buf) == '#' ||
        *DBufValue(&buf) == ';') {
        DBufFree(&buf);
        DumpVarTable(dump_constness);
        return OK;
    }
    fprintf(ErrFp, "%s  %s\n\n", VARIABLE, VALUE);
    while(1) {
        if (*DBufValue(&buf) == '$') {
            DumpSysVarByName(DBufValue(&buf)+1);
        } else {
            v = FindVar(DBufValue(&buf), 0);
            DBufValue(&buf)[VAR_NAME_LEN] = 0;
            if (!v) fprintf(ErrFp, "%s  %s\n",
                            DBufValue(&buf), UNDEF);
            else {
                fprintf(ErrFp, "%s  ", v->name);
                PrintValue(&(v->v), ErrFp);
                if (dump_constness) {
                    if (!v->nonconstant) {
                        fprintf(ErrFp, " <const>");
                    }
                }
                fprintf(ErrFp, "\n");
            }
        }
        r = ParseToken(p, &buf);
        if (r) return r;
        if (!*DBufValue(&buf) ||
            *DBufValue(&buf) == '#' ||
            *DBufValue(&buf) == ';') {
            DBufFree(&buf);
            return OK;
        }
    }
}

/***************************************************************/
/*                                                             */
/*  DumpVarTable                                               */
/*                                                             */
/*  Dump the variable table to ErrFp.                         */
/*                                                             */
/***************************************************************/
void DumpVarTable(int dump_constness)
{
    Var *v;

    fprintf(ErrFp, "%s  %s\n\n", VARIABLE, VALUE);

    hash_table_for_each(v, &VHashTbl) {
        fprintf(ErrFp, "%s  ", v->name);
        PrintValue(&(v->v), ErrFp);
        if (dump_constness) {
            if (!v->nonconstant) {
                fprintf(ErrFp, " <const>");
            }
        }
        fprintf(ErrFp, "\n");
    }
}

/***************************************************************/
/*                                                             */
/*  DestroyVars                                                */
/*                                                             */
/*  Free all the memory used by variables, but don't delete    */
/*  preserved variables unless ALL is non-zero.                */
/*                                                             */
/***************************************************************/
void DestroyVars(int all)
{
    Var *v;
    Var *next;

    v = hash_table_next(&VHashTbl, NULL);
    while(v) {
        next = hash_table_next(&VHashTbl, v);
        if (all || !v->preserve) {
            DestroyValue(v->v);
            hash_table_delete_no_resize(&VHashTbl, v);
            free(v);
        }
        v = next;
    }
    if (all) {
        hash_table_free(&VHashTbl);
        InitVars();
    }
}

/***************************************************************/
/*                                                             */
/*  PreserveVar                                                */
/*                                                             */
/*  Given the name of a variable, "preserve" it.               */
/*                                                             */
/***************************************************************/
int PreserveVar(char const *name)
{
    Var *v;

    v = FindVar(name, 1);
    if (!v) return E_NO_MEM;
    v->preserve = 1;
    return OK;
}

/***************************************************************/
/*                                                             */
/*  DoPreserve - preserve a bunch of variables.                */
/*                                                             */
/***************************************************************/
int DoPreserve (Parser *p)
{
    int r;

    DynamicBuffer buf;
    DBufInit(&buf);

    r = ParseToken(p, &buf);
    if (r) return r;
    if (!DBufLen(&buf)) {
        DBufFree(&buf);
        return E_EOLN;
    }

    r = PreserveVar(DBufValue(&buf));
    DBufFree(&buf);
    if (r) return r;

/* Keep going... */
    while(1) {
        r = ParseToken(p, &buf);
        if (r) return r;
        if (!DBufLen(&buf)) {
            DBufFree(&buf);
            return OK;
        }
        r = PreserveVar(DBufValue(&buf));
        DBufFree(&buf);
        if (r) return r;
    }
}

/***************************************************************/
/*                                                             */
/*  SYSTEM VARIABLES                                           */
/*                                                             */
/*  Interface for modifying and reading system variables.      */
/*                                                             */
/***************************************************************/

/* Macro to access "min" but as a constval.  Just to make source more
   readable */
#define constval min

/* If the type of a sys variable is STR_TYPE, then min is redefined
   to be a flag indicating whether or not the value has been malloc'd. */
#define been_malloced min

/* Flag for no min/max constraint */
#define ANY -31415926

/* All of the system variables sorted alphabetically */
static SysVar SysVarArr[] = {
    /*  name          mod  type              value          min/mal   max */
    {"AddBlankLines",  1,  INT_TYPE,     &AddBlankLines,       0,      1 },
    {"Ago",            1,  TRANS_TYPE,   "ago",                0,      0 },
    {"Am",             1,  TRANS_TYPE,   "am",                 0,      0 },
    {"And",            1,  TRANS_TYPE,   "and",                0,      0 },
    {"April",          1,  TRANS_TYPE,   "April",              0,      0 },
    {"At",             1,  TRANS_TYPE,   "at",                 0,      0 },
    {"August",         1,  TRANS_TYPE,   "August",             0,      0 },
    {"CalcUTC",        1,  INT_TYPE,     &CalculateUTC,        0,      1 },
    {"CalMode",        0,  INT_TYPE,     &DoCalendar,          0,      0 },
    {"CalType",        0,  STR_TYPE,     &CalType,             0,      0 },
    {"Daemon",         0,  INT_TYPE,     &Daemon,              0,      0 },
    {"DateSep",        1,  SPECIAL_TYPE, date_sep_func,        0,      0 },
    {"DateTimeSep",    1,  SPECIAL_TYPE, datetime_sep_func,    0,      0 },
    {"December",       1,  TRANS_TYPE,   "December",           0,      0 },
    {"DedupeReminders",1,  INT_TYPE,     &DedupeReminders,     0,      1 },
    {"DefaultColor",   1,  SPECIAL_TYPE, default_color_func,   0,      0 },
    {"DefaultDelta",   1,  INT_TYPE,     &DefaultDelta,        0,      10000 },
    {"DefaultPrio",    1,  INT_TYPE,     &DefaultPrio,         0,      9999 },
    {"DefaultTDelta",  1,  INT_TYPE,     &DefaultTDelta,       0,      MINUTES_PER_DAY },
    {"DeltaOverride",  0,  INT_TYPE,     &DeltaOverride,       0,      0 },
    {"DontFork",       0,  INT_TYPE,     &DontFork,            0,      0 },
    {"DontQueue",      0,  INT_TYPE,     &DontQueue,           0,      0 },
    {"DontTrigAts",    0,  INT_TYPE,     &DontIssueAts,        0,      0 },
    {"EndSent",        1,  STR_TYPE,     &EndSent,             0,      0 },
    {"EndSentIg",      1,  STR_TYPE,     &EndSentIg,           0,      0 },
    {"ExpressionTimeLimit", 1, SPECIAL_TYPE, expr_time_limit_func, 0,  0 },
    {"February",       1,  TRANS_TYPE,   "February",           0,      0 },
    {"FirstIndent",    1,  INT_TYPE,     &FirstIndent,         0,      132 },
    {"FoldYear",       1,  INT_TYPE,     &FoldYear,            0,      1 },
    {"FormWidth",      1,  INT_TYPE,     &FormWidth,           20,     500 },
    {"Friday",         1,  TRANS_TYPE,   "Friday",             0,      0 },
    {"Fromnow",        1,  TRANS_TYPE,   "from now",           0,      0 },
    {"Hour",           1,  TRANS_TYPE,   "hour",               0,      0 },
    {"Hplu",           1,  STR_TYPE,     &DynamicHplu,         0,      0 },
    {"HushMode",       0,  INT_TYPE,     &Hush,                0,      0 },
    {"IgnoreOnce",     0,  INT_TYPE,     &IgnoreOnce,          0,      0 },
    {"InfDelta",       0,  INT_TYPE,     &InfiniteDelta,       0,      0 },
    {"IntMax",         0,  INT_TYPE,     &IntMax,              0,      0 },
    {"IntMin",         0,  INT_TYPE,     &IntMin,              0,      0 },
    {"Is",             1,  TRANS_TYPE,   "is",                 0,      0 },
    {"January",        1,  TRANS_TYPE,   "January",            0,      0 },
    {"July",           1,  TRANS_TYPE,   "July",               0,      0 },
    {"June",           1,  TRANS_TYPE,   "June",               0,      0 },
    {"LatDeg",         1,  SPECIAL_TYPE, latdeg_func,          0,      0 },
    {"Latitude",       1,  SPECIAL_TYPE, latitude_func,        0,      0 },
    {"LatMin",         1,  SPECIAL_TYPE, latmin_func,          0,      0 },
    {"LatSec",         1,  SPECIAL_TYPE, latsec_func,          0,      0 },
    {"Location",       1,  STR_TYPE,     &Location,            0,      0 },
    {"LongDeg",        1,  SPECIAL_TYPE, longdeg_func,         0,      0 },
    {"Longitude",      1,  SPECIAL_TYPE, longitude_func,       0,      0 },
    {"LongMin",        1,  SPECIAL_TYPE, longmin_func,         0,      0 },
    {"LongSec",        1,  SPECIAL_TYPE, longsec_func,         0,      0 },
    {"March",          1,  TRANS_TYPE,   "March",              0,      0 },
    {"MaxFullOmits",   0,  CONST_INT_TYPE, NULL,        MAX_FULL_OMITS, 0},
    {"MaxLateMinutes", 1,  INT_TYPE,     &MaxLateMinutes,      0,      MINUTES_PER_DAY },
    {"MaxPartialOmits",0,  CONST_INT_TYPE, NULL,    MAX_PARTIAL_OMITS, 0},
    {"MaxSatIter",     1,  INT_TYPE,     &MaxSatIter,          10,     ANY },
    {"MaxStringLen",   1,  INT_TYPE,     &MaxStringLen,        -1,     ANY },
    {"May",            1,  TRANS_TYPE,   "May",                0,      0 },
    {"MinsFromUTC",    1,  INT_TYPE,     &MinsFromUTC,         -780,   780 },
    {"Minute",         1,  TRANS_TYPE,   "minute",             0,      0 },
    {"Monday",         1,  TRANS_TYPE,   "Monday",             0,      0 },
    {"Mplu",           1,  STR_TYPE,     &DynamicMplu,         0,      0 },
    {"NextMode",       0,  INT_TYPE,     &NextMode,            0,      0 },
    {"November",       1,  TRANS_TYPE,   "November",           0,      0 },
    {"Now",            1,  TRANS_TYPE,   "now",                0,      0 },
    {"NumFullOmits",   0,  INT_TYPE,     &NumFullOmits,        0,      0 },
    {"NumPartialOmits",0,  INT_TYPE,     &NumPartialOmits,     0,      0 },
    {"NumQueued",      0,  INT_TYPE,     &NumQueued,           0,      0 },
    {"NumTrig",        0,  INT_TYPE,     &NumTriggered,        0,      0 },
    {"October",        1,  TRANS_TYPE,   "October",            0,      0 },
    {"On",             1,  TRANS_TYPE,   "on",                 0,      0 },
    {"OnceFile",       1,  SPECIAL_TYPE, oncefile_func,        0,      0 },
    {"ParseUntriggered", 1, INT_TYPE,    &ParseUntriggered,    0,      1 },
    {"Pm",             1,  TRANS_TYPE,   "pm",                 0,      0 },
    {"PrefixLineNo",   0,  INT_TYPE,     &DoPrefixLineNo,      0,      0 },
    {"PSCal",          0,  INT_TYPE,     &PsCal,               0,      0 },
    {"RunOff",         0,  INT_TYPE,     &RunDisabled,         0,      0 },
    {"Saturday",       1,  TRANS_TYPE,   "Saturday",           0,      0 },
    {"September",      1,  TRANS_TYPE,   "September",          0,      0 },
    {"SimpleCal",      0,  INT_TYPE,     &DoSimpleCalendar,    0,      0 },
    {"SortByDate",     0,  INT_TYPE,     &SortByDate,          0,      0 },
    {"SortByPrio",     0,  INT_TYPE,     &SortByPrio,          0,      0 },
    {"SortByTime",     0,  INT_TYPE,     &SortByTime,          0,      0 },
    {"SubsIndent",     1,  INT_TYPE,     &SubsIndent,          0,      132 },
    {"Sunday",         1,  TRANS_TYPE,   "Sunday",             0,      0 },
    {"SuppressImplicitWarnings", 1, INT_TYPE, &SuppressImplicitRemWarnings, 0, 1},
    {"SuppressLRM",    1,  INT_TYPE,     &SuppressLRM,         0,      1 },
    {"SysInclude",     0,  STR_TYPE,     &SysDir,              0,      0 },
    {"T",              0,  SPECIAL_TYPE, trig_date_func,       0,      0 },
    {"Td",             0,  SPECIAL_TYPE, trig_day_func,        0,      0 },
    {"TerminalBackground", 0, SPECIAL_TYPE,  terminal_bg_func, 0,      0 },
    {"Thursday",       1,  TRANS_TYPE,   "Thursday",           0,      0 },
    {"TimeSep",        1,  SPECIAL_TYPE, time_sep_func,        0,      0 },
    {"TimetIs64bit",   0,  SPECIAL_TYPE, timet_is_64_func,     0,      0 },
    {"Tm",             0,  SPECIAL_TYPE, trig_mon_func,        0,      0 },
    {"Today",          1,  TRANS_TYPE,   "today",              0,      0 },
    {"Tomorrow",       1,  TRANS_TYPE,   "tomorrow",           0,      0 },
    {"Tt",             0,  SPECIAL_TYPE, trig_time_func,       0,      0 },
    {"Tuesday",        1,  TRANS_TYPE,   "Tuesday",            0,      0 },
    {"Tw",             0,  SPECIAL_TYPE, trig_wday_func,       0,      0 },
    {"Ty",             0,  SPECIAL_TYPE, trig_year_func,       0,      0 },
    {"U",              0,  SPECIAL_TYPE, today_date_func,      0,      0 },
    {"Ud",             0,  SPECIAL_TYPE, today_day_func,       0,      0 },
    {"Um",             0,  SPECIAL_TYPE, today_mon_func,       0,      0 },
    {"UntimedFirst",   0,  INT_TYPE,     &UntimedBeforeTimed,  0,      0 },
    {"Use256Colors",   0,  INT_TYPE,     &Use256Colors,        0,      0 },
    {"UseBGVTColors",  0,  INT_TYPE,     &UseBGVTColors,       0,      0 },
    {"UseTrueColors",  0,  INT_TYPE,     &UseTrueColors,       0,      0 },
    {"UseVTColors",    0,  INT_TYPE,     &UseVTColors,         0,      0 },
    {"Uw",             0,  SPECIAL_TYPE, today_wday_func,      0,      0 },
    {"Uy",             0,  SPECIAL_TYPE, today_year_func,      0,      0 },
    {"Was",            1,  TRANS_TYPE,   "was",                0,      0 },
    {"Wednesday",      1,  TRANS_TYPE,   "Wednesday",          0,      0 }
};

#define NUMSYSVARS ( sizeof(SysVarArr) / sizeof(SysVar) )
static void DumpSysVar (char const *name, const SysVar *v);

static int SetTranslatableVariable(SysVar *v, Value *value)
{
    return InsertTranslation((char const *) v->value, value->v.str);
}

static int GetTranslatableVariable(SysVar *v, Value *value)
{
    char const *translated = tr((char const *) v->value);
    if (translated) {
        value->v.str = StrDup(translated);
    } else {
        value->v.str = StrDup("");
    }
    if (!value->v.str) return E_NO_MEM;
    value->type = STR_TYPE;
    return OK;
}

static int SetSysVarHelper(SysVar *v, Value *value)
{
    int r;
    if (!v->modifiable) {
        Eprint("%s: `$%s'", GetErr(E_CANT_MODIFY), v->name);
        return E_CANT_MODIFY;
    }

    if (v->type == TRANS_TYPE) {
        if (value->type != STR_TYPE) return E_BAD_TYPE;
        r =  SetTranslatableVariable(v, value);
        DestroyValue(*value);
        return r;
    }

    if (v->type != SPECIAL_TYPE &&
        v->type != value->type) return E_BAD_TYPE;
    if (v->type == SPECIAL_TYPE) {
        SysVarFunc f = (SysVarFunc) v->value;
        r = f(1, value);
        DestroyValue(*value);
        return r;
    }

    if (v->type == STR_TYPE) {
        /* If it's already the same, don't bother doing anything */
        if (!strcmp(value->v.str, * (char const **) v->value)) {
            DestroyValue(*value);
            return OK;
        }

        /* If it's a string variable, special measures must be taken */
        if (v->been_malloced) free(*((char **)(v->value)));
        v->been_malloced = 1;
        *((char **) v->value) = value->v.str;
        value->type = ERR_TYPE;  /* So that it's not accidentally freed */
    } else {
        if (v->max != ANY && value->v.val > v->max) return E_2HIGH;
        if (v->min != ANY && value->v.val < v->min) return E_2LOW;
        *((int *)v->value) = value->v.val;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  SetSysVar                                                  */
/*                                                             */
/*  Set a system variable to the indicated value.              */
/*                                                             */
/***************************************************************/
int SetSysVar(char const *name, Value *value)
{
    SysVar *v = FindSysVar(name);
    if (!v) return E_NOSUCH_VAR;
    return SetSysVarHelper(v, value);
}

/***************************************************************/
/*                                                             */
/*  GetSysVar                                                  */
/*                                                             */
/*  Get the value of a system variable                         */
/*                                                             */
/***************************************************************/
int GetSysVar(char const *name, Value *val)
{
    SysVar *v = FindSysVar(name);

    val->type = ERR_TYPE;
    if (!v) return E_NOSUCH_VAR;
    if (v->type == TRANS_TYPE) {
        return GetTranslatableVariable(v, val);
    }

    if (v->type == CONST_INT_TYPE) {
        val->v.val = v->constval;
        val->type = INT_TYPE;
        return OK;
    }
    if (v->type == SPECIAL_TYPE) {
        SysVarFunc f = (SysVarFunc) v->value;
        return f(0, val);
    } else if (v->type == STR_TYPE) {
        if (! * (char **) v->value) {
            val->v.str = StrDup("");
        } else {
            val->v.str = StrDup(*((char **) v->value));
        }
        if (!val->v.str) return E_NO_MEM;
    } else {
        val->v.val = *((int *) v->value);
    }
    val->type = v->type;

    /* In "verbose" mode, print attempts to test $RunOff */
    if (DebugFlag & DB_PRTLINE) {
        if (v->value == (void *) &RunDisabled) {
            Wprint(tr("(Security note: $RunOff variable tested.)"));
        }
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/* FindSysVar                                                  */
/*                                                             */
/* Find a system var with specified name.                      */
/*                                                             */
/***************************************************************/
SysVar *FindSysVar(char const *name)
{
    int top=NUMSYSVARS-1, bottom=0;
    int mid=(top + bottom) / 2;
    int r;

    while (top >= bottom) {
        r = StrCmpi(name, SysVarArr[mid].name);
        if (!r) return &SysVarArr[mid];
        else if (r>0) bottom = mid+1;
        else        top = mid-1;
        mid = (top+bottom) / 2;
    }
    return NULL;
}

/***************************************************************/
/*                                                             */
/*  DumpSysVarByName                                           */
/*                                                             */
/*  Given the name of a system variable, display it.           */
/*  If name is "", dump all system variables.                  */
/*                                                             */
/***************************************************************/
void DumpSysVarByName(char const *name)
{
    size_t i;
    SysVar *v;

    if (!name || !*name) {
        for (i=0; i<NUMSYSVARS; i++) DumpSysVar(name, SysVarArr + i);
        return;
    }

    v = FindSysVar(name);
    DumpSysVar(name, v);
    return;
}

/***************************************************************/
/*                                                             */
/*  DumpSysVar                                                 */
/*                                                             */
/*  Dump the system variable.                                  */
/*                                                             */
/***************************************************************/
static void DumpSysVar(char const *name, const SysVar *v)
{
    char buffer[VAR_NAME_LEN+10];
    Value vtmp;

    if (name && !*name) name=NULL;
    if (!v && !name) return;  /* Shouldn't happen... */

    buffer[0]='$'; buffer[1] = 0;
    if (name && strlen(name) > VAR_NAME_LEN) {
        fprintf(ErrFp, "$%s: Name too long\n", name);
        return;
    }
    if (name) strcat(buffer, name); else strcat(buffer, v->name);
    fprintf(ErrFp, "%25s  ", buffer);
    if (v) {
        if (v->type == CONST_INT_TYPE) {
            fprintf(ErrFp, "%d\n", v->constval);
        } else if (v->type == SPECIAL_TYPE) {
            SysVarFunc f = (SysVarFunc) v->value;
            f(0, &vtmp);
            PrintValue(&vtmp, ErrFp);
            putc('\n', ErrFp);
            DestroyValue(vtmp);
        } else if (v->type == TRANS_TYPE) {
            int r = GetSysVar(v->name, &vtmp);
            if (r == OK) {
                PrintValue(&vtmp, ErrFp);
                putc('\n', ErrFp);
                DestroyValue(vtmp);
            } else {
                fprintf(ErrFp, "Error: %s\n", GetErr(r));
            }
        } else if (v->type == STR_TYPE) {
            vtmp.type = STR_TYPE;
            vtmp.v.str = * ((char **)v->value);
            PrintValue(&vtmp, ErrFp);
            putc('\n', ErrFp);
        } else if (v->type == DATE_TYPE) {
            vtmp.type = DATE_TYPE;
            vtmp.v.val = * (int *) v->value;
            PrintValue(&vtmp, ErrFp);
            putc('\n', ErrFp);
        } else {
            if (!v->modifiable) fprintf(ErrFp, "%d\n", *((int *)v->value));
            else {
                fprintf(ErrFp, "%-10d  ", *((int *)v->value));
                if (v->min == ANY) fprintf(ErrFp, "(-Inf, ");
                else                         fprintf(ErrFp, "[%d, ", v->min);
                if (v->max == ANY) fprintf(ErrFp, "Inf)\n");
                else                         fprintf(ErrFp, "%d]\n", v->max);
            }
        }
    } else   fprintf(ErrFp, "%s\n", UNDEF);

    return;
}

void
set_lat_and_long_from_components(void)
{
    Latitude = (double) LatDeg + ((double) LatMin) / 60.0 + ((double) LatSec) / 3600.0;
    Longitude = - ( (double) LongDeg + ((double) LongMin) / 60.0 + ((double) LongSec) / 3600.0);
}

void
set_components_from_lat_and_long(void)
{
    double x;

    x = (Latitude < 0.0 ? -Latitude : Latitude);
    LatDeg = (int) x;
    x -= (double) LatDeg;
    x *= 60;
    LatMin = (int) x;
    x -= (double) LatMin;
    x *= 60;
    LatSec = (int) x;
    if (Latitude < 0.0) {
        LatDeg = -LatDeg;
        LatMin = -LatMin;
        LatSec = -LatSec;
    }

    x = (Longitude < 0.0 ? -Longitude : Longitude);
    LongDeg = (int) x;
    x -= (double) LongDeg;
    x *= 60;
    LongMin = (int) x;
    x -= (double) LongMin;
    x *= 60;
    LongSec = (int) x;

    /* Use STANDARD sign for $Longitude even if $LongDeg, $LongMin and
     * $LongSec are messed up */
    if (Longitude > 0.0) {
        LongDeg = -LongDeg;
        LongMin = -LongMin;
        LongSec = -LongSec;
    }
}

void GenerateSysvarTranslationTemplates(void)
{
    int i;
    int j;
    char const *msg;
    for (i=0; i< (int) NUMSYSVARS; i++) {
        if (SysVarArr[i].type == TRANS_TYPE) {
            int done=0;
            msg = (char const *) SysVarArr[i].value;
            /* We've already done month and day names */
            for (j=0; j<7; j++) {
                if (!strcmp(msg, DayName[j])) {
                    done=1;
                    break;
                }
            }
            if (done) {
                continue;
            }
            for (j=0; j<12; j++) {
                if (!strcmp(msg, MonthName[j])) {
                    done=1;
                    break;
                }
            }
            if (done) {
                continue;
            }
            printf("SET $%s ", SysVarArr[i].name);
            print_escaped_string_helper(stdout, tr(msg), 1, 0);
            printf("\n");
        } else if (!strcmp(SysVarArr[i].name, "Hplu") ||
                   !strcmp(SysVarArr[i].name, "Mplu")) {
            msg = * (char const **) SysVarArr[i].value;
            printf("SET $%s ", SysVarArr[i].name);
            print_escaped_string_helper(stdout, tr(msg), 1, 0);
            printf("\n");
        }
    }
}
void
print_sysvar_tokens(void)
{
    int i;
    printf("\n# System Variables\n\n");
    for (i=0; i< (int) NUMSYSVARS; i++) {
        printf("$%s\n", SysVarArr[i].name);
    }
}

void
dump_var_hash_stats(void)
{
    hash_table_dump_stats(&VHashTbl, ErrFp);
}
