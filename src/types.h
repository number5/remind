/***************************************************************/
/*                                                             */
/*  TYPES.H                                                    */
/*                                                             */
/*  Type definitions all dumped here.                          */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include <limits.h>
#include "dynbuf.h"

typedef struct udf_struct UserFunc;

/* Define the types of values */
#define ERR_TYPE       0
#define INT_TYPE       1
#define TIME_TYPE      2
#define DATE_TYPE      3
#define STR_TYPE       4
#define DATETIME_TYPE  5
#define SPECIAL_TYPE   6 /* Only for system variables */
#define CONST_INT_TYPE 7 /* Only for system variables */

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
    N_ERROR = 32767,
};

/* Structure for passing in Nargs and out RetVal from functions */
typedef struct {
    int nargs;
    Value *args;
    Value retval;
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
    struct var *next;
    char name[VAR_NAME_LEN+1];
    char preserve;
    Value v;
} Var;

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
#define DB_PRTLINE      1
#define DB_PRTEXPR      2
#define DB_PRTTRIG      4
#define DB_DUMP_VARS    8
#define DB_ECHO_LINE   16
#define DB_TRACE_FILES 32
#define DB_PARSE_EXPR  64

/* Enumeration of the tokens */
enum TokTypes
{ T_Illegal,
  /* Commands first */
  T_Rem, T_Push, T_Pop, T_Preserve, T_Include, T_IncludeR, T_IncludeCmd, T_If, T_Else, T_EndIf,
  T_IfTrig, T_ErrMsg,
  T_Set, T_UnSet, T_Fset, T_Funset, T_Omit, T_Banner, T_Exit,
  T_AddOmit, T_NoQueue,
  T_WkDay,
  T_Month, T_Time, T_Date, T_DateTime,
  T_Skip, T_At, T_RemType, T_Until, T_Year, T_Day, T_Rep, T_Delta,
  T_Back, T_BackAdj,
  T_Once,
  T_Empty,
  T_Comment,
  T_Number,
  T_Clr,
  T_Debug,
  T_Dumpvars,
  T_Scanfrom,
  T_Flush,
  T_Priority,
  T_Sched,
  T_Warn,
  T_Tag,
  T_Duration,
  T_LongTime,
  T_OmitFunc,
  T_Through,
  T_MaybeUncomputable,
  T_Ordinal,
  T_In,
  T_LastBack,
  T_Expr
};

/* The structure of a token */
typedef struct {
    char *name;
    char MinLen;
    enum TokTypes type;
    int val;
} Token;

/* Flags for the state of the "if" stack */
#define IF_TRUE      0
#define IF_FALSE     1
#define BEFORE_ELSE  0
#define AFTER_ELSE   2
#define IF_MASK      3
#define IF_TRUE_MASK 1
#define IF_ELSE_MASK 2

/* Flags for the DoSubst function */
#define NORMAL_MODE  0
#define CAL_MODE     1
#define ADVANCE_MODE 2

#define QUOTE_MARKER 1 /* Unlikely character to appear in reminder */

/* Flags for disabling run */
#define RUN_CMDLINE  1
#define RUN_SCRIPT   2
#define RUN_NOTOWNER 4

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
    struct udf_struct *next;
    char name[VAR_NAME_LEN+1];
    expr_node *node;
    char **args;
    int nargs;
    char const *filename;
    int lineno;
    int recurse_flag;
} UserFunc;
