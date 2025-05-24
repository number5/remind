/***************************************************************/
/*                                                             */
/*  TYPES.H                                                    */
/*                                                             */
/*  Type definitions all dumped here.                          */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include <limits.h>
#include "dynbuf.h"
#include "hashtab.h"

typedef struct udf_struct UserFunc;

/* Define the types of values.  We use bitmasks so we can define
   DATETIME_TYPE as a combo of DATE_TYPE and TIME_TYPE */

#define ERR_TYPE       0x0
#define INT_TYPE       0x1
#define TIME_TYPE      0x2
#define DATE_TYPE      0x4
/* DATETIME_TYPE has both DATE and TIME bits turned on */
#define DATETIME_TYPE  (TIME_TYPE | DATE_TYPE)
#define STR_TYPE       0x8
#define SPECIAL_TYPE   0x10 /* Only for system variables */
#define CONST_INT_TYPE 0x20 /* Only for system variables */
#define TRANS_TYPE     0x40 /* Only for system variables */

#define BEG_OF_EXPR '['
#define END_OF_EXPR ']'
#define COMMA ','

/* Values */
typedef struct {
    char type;
    union {
        char *str;
        int val;
    } v;
} Value;

/* New-style expr_node structure and constants */
enum expr_node_type
{
    N_FREE = 0,
    N_CONSTANT,
    N_SHORT_STR,
    N_LOCAL_VAR,
    N_SHORT_VAR,
    N_VARIABLE,
    N_SHORT_SYSVAR,
    N_SYSVAR,
    N_BUILTIN_FUNC,
    N_SHORT_USER_FUNC,
    N_USER_FUNC,
    N_OPERATOR,
    N_ERROR = 0x7FFF,
};

/* Structure for passing in Nargs and out RetVal from functions */
typedef struct {
    int nargs;
    Value *args;
    Value retval;
    int nonconst;
} func_info;

/* Forward reference */
typedef struct expr_node_struct expr_node;

/* Define the type of user-functions */
typedef struct {
    char const *name;
    char minargs;
    char maxargs;
    char is_constant;
    /* Old-style function calling convention */
    int (*func)(func_info *);

    /* New-style function calling convention */
    int (*newfunc)(expr_node *node, Value *locals, Value *ans, int *nonconst);
} BuiltinFunc;

#define SHORT_NAME_BUF 16
typedef struct expr_node_struct {
    struct expr_node_struct *child;
    struct expr_node_struct *sibling;
    enum expr_node_type type;
    int num_kids;
    union {
        Value value;
        int arg;
        BuiltinFunc *builtin_func;
        char name[SHORT_NAME_BUF];
        int (*operator_func) (struct expr_node_struct *node, Value *locals, Value *ans, int *nonconst);
    } u;
} expr_node;

/* Define the structure of a variable */
typedef struct var {
    struct hash_link link;
    char name[VAR_NAME_LEN+1];
    char preserve;
    char is_constant;
    char used_since_set;
    Value v;
} Var;

typedef struct triginfo {
    struct triginfo *next;
    char const *info;
} TrigInfo;

/* A trigger */
typedef struct {
    int expired;
    int wd;
    int d;
    int m;
    int y;
    int back;
    int delta;
    int rep;
    int localomit;
    int skip;
    int until;
    int typ;
    int once;
    int scanfrom;
    int from;
    int adj_for_last;            /* Adjust month/year for use of LAST */
    int need_wkday;              /* Set if we *need* a weekday */
    int priority;
    int duration_days;           /* Duration converted to days to search */
    int eventstart;              /* Original event start (datetime) */
    int eventduration;           /* Original event duration (minutes) */
    int maybe_uncomputable;      /* Suppress "can't compute trigger" warnings */
    int addomit;                 /* Add trigger date to global OMITs */
    int noqueue;                 /* Don't queue even if timed */
    char sched[VAR_NAME_LEN+1];  /* Scheduling function */
    char warn[VAR_NAME_LEN+1];   /* Warning function    */
    char omitfunc[VAR_NAME_LEN+1]; /* OMITFUNC function */
    DynamicBuffer tags;
    char passthru[PASSTHRU_LEN+1];
    TrigInfo *infos;
} Trigger;

/* A time trigger */
typedef struct {
    int ttime;
    int nexttime;
    int delta;
    int rep;
    int duration;
} TimeTrig;

/* The parse pointer */
typedef struct {
    DynamicBuffer pushedToken;  /* Pushed-back token */
    char const *text;           /* Start of text */
    char const *pos;            /* Current position */
    char const *etext;          /* Substituted text */
    char const *epos;           /* Position in substituted text */
    char const *tokenPushed;    /* NULL if no pushed-back token */
    unsigned char isnested;      /* Is it a nested expression? */
    unsigned char allownested;
    unsigned char expr_happened; /* Did we encounter an [expression] ? */
    unsigned char nonconst_expr; /* Did we encounter a non-constant [expression] ? */
} Parser;

typedef Parser *ParsePtr;  /* Pointer to parser structure */

/* Some useful manifest constants */
#define NO_BACK 0
#define NO_DELTA 0
#define NO_REP 0
#define NO_WD 0
#define NO_DAY -1
#define NO_MON -1
#define NO_YR -1
#define NO_UNTIL -1
#define NO_ONCE 0
#define ONCE_ONCE 1
#define NO_DATE -1
#define NO_SKIP 0
#define SKIP_SKIP 1
#define BEFORE_SKIP 2
#define AFTER_SKIP 3

#define NO_TIME INT_MAX

#define NO_PRIORITY 5000 /* Default priority is midway between 0 and 9999 */

#define NO_TYPE  0
#define MSG_TYPE 1
#define RUN_TYPE 2
#define CAL_TYPE 3
#define SAT_TYPE 4
#define PS_TYPE  5
#define PSF_TYPE 6
#define MSF_TYPE 7
#define PASSTHRU_TYPE 8

/* For function arguments */
#define NO_MAX 127

/* DEFINES for debugging flags */
#define DB_PRTLINE      0x001
#define DB_PRTEXPR      0x002
#define DB_PRTTRIG      0x004
#define DB_DUMP_VARS    0x008
#define DB_ECHO_LINE    0x010
#define DB_TRACE_FILES  0x020
#define DB_PARSE_EXPR   0x040
#define DB_HASHSTATS    0x080
#define DB_TRANSLATE    0x100
#define DB_NONCONST     0x200
#define DB_UNUSED_VARS  0x400

/* Enumeration of the tokens */
enum TokTypes
{ T_Illegal,
  T_AddOmit, T_At, T_Back, T_BackAdj, T_Banner, T_Clr, T_Comment,
  T_Date, T_DateTime, T_Day, T_Debug, T_Delta, T_Dumpvars, T_Duration,
  T_Else, T_Empty, T_EndIf, T_ErrMsg, T_Exit, T_Expr,
  T_Flush, T_Frename, T_Fset, T_Funset, T_If, T_IfTrig, T_In,
  T_Include, T_IncludeCmd, T_IncludeR, T_IncludeSys, T_Info, T_LastBack,
  T_LongTime, T_MaybeUncomputable, T_Month, T_NoQueue, T_Number, T_Omit,
  T_OmitFunc, T_Once, T_Ordinal, T_Pop, T_Preserve, T_Priority, T_Push,T_Rem,
  T_RemType, T_Rep, T_Scanfrom, T_Sched, T_Set, T_Skip, T_Tag, T_Through,
  T_Time, T_Translate, T_UnSet, T_Until, T_Warn, T_WkDay, T_Year
};

/* The structure of a token */
typedef struct {
    char const *name;
    char MinLen;
    enum TokTypes type;
    int val;
} Token;

/* Flags for the DoSubst function */
#define NORMAL_MODE  0
#define CAL_MODE     1
#define ADVANCE_MODE 2

#define QUOTE_MARKER 1 /* Unlikely character to appear in reminder */

/* Flags for disabling run */
#define RUN_CMDLINE  0x01
#define RUN_SCRIPT   0x02
#define RUN_NOTOWNER 0x04
#define RUN_IN_EVAL  0x08

/* Flags for the SimpleCalendar format */
#define SC_AMPM   0   /* Time shown as 3:00am, etc. */
#define SC_MIL    1   /* 24-hour time format */
#define SC_NOTIME 2   /* Do not display time in SC format. */

/* Flags for sorting */
#define SORT_NONE    0
#define SORT_ASCEND  1
#define SORT_DESCEND 2

/* Flags for FROM / SCANFROM */
#define SCANFROM_TYPE 0
#define FROM_TYPE     1

/* PS Calendar levels */

/* Original interchange format */
#define PSCAL_LEVEL1  1

/* Line-by-line JSON */
#define PSCAL_LEVEL2  2

/* Pure JSON */
#define PSCAL_LEVEL3  3

#define TERMINAL_BACKGROUND_UNKNOWN -1
#define TERMINAL_BACKGROUND_DARK    0
#define TERMINAL_BACKGROUND_LIGHT   1

typedef int (*SysVarFunc)(int, Value *);
/* The structure of a system variable */
typedef struct {
    char const *name;
    char modifiable;
    int type;
    void *value;
    int min; /* Or const-value */
    int max;
} SysVar;

/* Define the data structure used to hold a user-defined function */
typedef struct udf_struct {
    struct hash_link link;
    char name[VAR_NAME_LEN+1];
    char is_constant;
    expr_node *node;
    char **args;
    int nargs;
    char const *filename;
    int lineno;
    int lineno_start;
    int recurse_flag;
} UserFunc;
