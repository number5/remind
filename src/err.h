/***************************************************************/
/*                                                             */
/*  ERR.H                                                      */
/*                                                             */
/*  Error definitions.                                         */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/* Note that not all of the "errors" are really errors - some are just
   messages for information purposes.  Constants beginning with M_ should
   never be returned as error indicators - they should only be used to
   index the ErrMsg array. */

#define OK                    0
#define E_MISS_END            1
#define E_MISS_QUOTE          2
#define E_OP_STK_OVER         3
#define E_VA_STK_OVER         4
#define E_MISS_RIGHT_PAREN    5
#define E_UNDEF_FUNC          6
#define E_ILLEGAL_CHAR        7
#define E_EXPECTING_BINOP     8
#define E_NO_MEM              9
#define E_BAD_NUMBER         10
#define E_OP_STK_UNDER       11
#define E_VA_STK_UNDER       12
#define E_CANT_COERCE        13
#define E_BAD_TYPE           14
#define E_DATE_OVER          15
#define E_STACK_ERR          16
#define E_DIV_ZERO           17
#define E_NOSUCH_VAR         18
#define E_EOLN		     19
#define E_EOF		     20
#define E_IO_ERR             21
#define E_LINE_2_LONG        22
#define E_SWERR		     23
#define E_BAD_DATE           24
#define E_2FEW_ARGS	     25
#define E_2MANY_ARGS	     26
#define E_BAD_TIME	     27
#define E_2HIGH		     28
#define E_2LOW 		     29
#define E_CANT_OPEN          30
#define E_NESTED_INCLUDE     31
#define E_PARSE_ERR          32
#define E_CANT_TRIG          33
#define E_NESTED_IF          34
#define E_ELSE_NO_IF         35
#define E_ENDIF_NO_IF        36
#define E_2MANY_LOCALOMIT    37
#define E_EXTRANEOUS_TOKEN   38
#define E_POP_NO_PUSH        39
#define E_RUN_DISABLED       40
#define E_DOMAIN_ERR         41
#define E_BAD_ID             42
#define E_RECURSIVE          43
#define E_PARSE_AS_REM       44 /* Not really an error - just returned by
                                   DoOmit to indicate line should be executed
                                   as a REM statement, also. */
#define E_CANT_MODIFY        45
#define E_MKTIME_PROBLEM     46
#define E_REDEF_FUNC	     47
#define E_CANTNEST_FDEF      48
#define E_REP_FULSPEC        49
#define E_YR_TWICE	     50
#define E_MON_TWICE	     51
#define E_DAY_TWICE	     52
#define E_UNKNOWN_TOKEN	     53
#define E_SPEC_MON    	     54
#define E_2MANY_PART	     55
#define E_2MANY_FULL	     56
#define E_PUSH_NOPOP	     57
#define E_ERR_READING	     58
#define E_EXPECTING_EOL	     59
#define E_BAD_HEBDATE	     60
#define E_IIF_ODD	     61
#define E_MISS_ENDIF	     62
#define E_EXPECT_COMMA	     63
#define E_WD_TWICE	     64
#define E_SKIP_ERR	     65
#define E_CANT_NEST_RTYPE    66
#define E_REP_TWICE	     67
#define E_DELTA_TWICE	     68
#define E_BACK_TWICE	     69
#define E_ONCE_TWICE	     70
#define E_EXPECT_TIME	     71
#define E_UNTIL_TWICE	     72
#define E_INCOMPLETE	     73
#define E_SCAN_TWICE	     74
#define E_VAR		     75
#define E_VAL		     76
#define E_UNDEF		     77
#define E_ENTER_FUN	     78
#define E_LEAVE_FUN	     79
#define E_EXPIRED	     80
#define E_CANTFORK	     81
#define E_CANTACCESS	     82
#define M_BAD_SYS_DATE	     83
#define M_BAD_DB_FLAG	     84
#define M_BAD_OPTION	     85
#define M_BAD_USER	     86
#define M_NO_CHG_GID	     87
#define M_NO_CHG_UID	     88
#define M_NOMEM_ENV	     89
#define E_MISS_EQ	     90
#define E_MISS_VAR	     91
#define E_MISS_EXPR	     92
#define M_CANTSET_ACCESS     93
#define M_I_OPTION	     94
#define E_NOREMINDERS	     95
#define M_QUEUED	     96
#define E_EXPECTING_NUMBER   97
#define M_BAD_WARN_FUNC      98
#define E_CANT_CONVERT_TZ    99
#define E_NO_MATCHING_REMS  100
#define E_STRING_TOO_LONG   101
#define E_TIME_TWICE        102
#define E_DURATION_NO_AT    103
#define E_EXPECTING_WEEKDAY 104
#define E_REPEATED_ARG      105
#define E_EXPR_DISABLED     106
#define E_TIME_EXCEEDED     107

#ifdef MK_GLOBALS
#undef EXTERN
#define EXTERN
#else
#undef EXTERN
#define EXTERN extern
#endif

#define STR(X) STR2(X)
#define STR2(X) #X


#ifndef L_ERR_OVERRIDE
EXTERN char *ErrMsg[]

#ifdef MK_GLOBALS
= {
/* OK */                  "Ok",
/* E_MISS_END */          "Missing ']'",
/* E_MISS_QUOTE */        "Missing quote",
/* E_OP_STK_OVER */       "Expression too complex",
/* E_VA_STK_OVER */       "Expression too complex - too many operands",
/* E_MISS_RIGHT_PAREN */  "Missing ')'",
/* E_UNDEF_FUNC */        "Undefined function",
/* E_ILLEGAL_CHAR */      "Illegal character",
/* E_EXPECTING_BINOP */   "Expecting binary operator",
/* E_NO_MEM */            "Out of memory",
/* E_BAD_NUMBER */        "Ill-formed number",
/* E_OP_STK_UNDER */      "Op stack underflow - internal error",
/* E_VA_STK_UNDER */      "Va stack underflow - internal error",
/* E_CANT_COERCE */       "Can't coerce",
/* E_BAD_TYPE */          "Type mismatch",
/* E_DATE_OVER */         "Date overflow",
/* E_STACK_ERR */         "Stack error - internal error",
/* E_DIV_ZERO */          "Division by zero",
/* E_NOSUCH_VAR */        "Undefined variable",
/* E_EOLN */              "Unexpected end of line",
/* E_EOF */               "Unexpected end of file",
/* E_IO_ERR */            "I/O error",
/* E_LINE_2_LONG */       "Line too long",
/* E_SWERR */             "Internal error",
/* E_BAD_DATE */          "Bad date specification",
/* E_2FEW_ARGS */         "Not enough arguments",
/* E_2MANY_ARGS */        "Too many arguments",
/* E_BAD_TIME */          "Ill-formed time",
/* E_2HIGH */             "Number too high",
/* E_2LOW */              "Number too low",
/* E_CANT_OPEN */         "Can't open file",
/* E_NESTED_INCLUDE */    "INCLUDE nested too deeply (max. " STR(INCLUDE_NEST) ")",
/* E_PARSE_ERR */         "Parse error",
/* E_CANT_TRIG */         "Can't compute trigger",
/* E_NESTED_IF */         "Too many nested IFs",
/* E_ELSE_NO_IF */        "ELSE with no matching IF",
/* E_ENDIF_NO_IF */       "ENDIF with no matching IF",
/* E_2MANY_LOCALOMIT */   "Can't OMIT every weekday",
/* E_EXTRANEOUS_TOKEN */  "Extraneous token(s) on line",
/* E_POP_NO_PUSH */       "POP-OMIT-CONTEXT without matching PUSH-OMIT-CONTEXT",
/* E_RUN_DISABLED */      "RUN disabled",
/* E_DOMAIN_ERR */        "Domain error",
/* E_BAD_ID */            "Invalid identifier",
/* E_RECURSIVE */         "Too many recursive function calls",
/* E_PARSE_AS_REM */      "",
/* E_CANT_MODIFY */       "Cannot modify system variable",
/* E_MKTIME_PROBLEM */    "C library function can't represent date/time",
/* E_REDEF_FUNC */        "Attempt to redefine built-in function",
/* E_CANTNEST_FDEF */     "Can't nest function definition in expression",
/* E_REP_FULSPEC */       "Must fully specify date to use repeat factor",
/* E_YR_TWICE */          "Year specified twice",
/* E_MON_TWICE */         "Month specified twice",
/* E_DAY_TWICE */         "Day specified twice",
/* E_UNKNOWN_TOKEN */     "Unknown token",
/* E_SPEC_MON */          "Must specify month in OMIT command",
/* E_2MANY_PART */        "Too many partial OMITs (max. " STR(MAX_PARTIAL_OMITS) ")",
/* E_2MANY_FULL */        "Too many full OMITs (max. " STR(MAX_FULL_OMITS) ")",
/* E_PUSH_NOPOP */        "Warning: PUSH-OMIT-CONTEXT without matching POP-OMIT-CONTEXT",
/* E_ERR_READING */       "Error reading",
/* E_EXPECTING_EOL */     "Expecting end-of-line",
/* E_BAD_HEBDATE */       "Invalid Hebrew date",
/* E_IIF_ODD */           "iif(): odd number of arguments required",
/* E_MISS_ENDIF */        "Warning: Missing ENDIF",
/* E_EXPECT_COMMA */      "Expecting comma",
/* E_WD_TWICE */          "Weekday specified twice",
/* E_SKIP_ERR */          "Only use one of BEFORE, AFTER or SKIP",
/* E_CANT_NEST_RTYPE */   "Can't nest MSG, MSF, RUN, etc. in expression",
/* E_REP_TWICE */         "Repeat value specified twice",
/* E_DELTA_TWICE */       "Delta value specified twice",
/* E_BACK_TWICE */        "Back value specified twice",
/* E_ONCE_TWICE */        "ONCE keyword used twice. (Hah.)",
/* E_EXPECT_TIME */       "Expecting time after AT",
/* E_UNTIL_TWICE */       "THROUGH/UNTIL keyword used twice",
/* E_INCOMPLETE */        "Incomplete date specification",
/* E_SCAN_TWICE */        "FROM/SCANFROM keyword used twice",
/* E_VAR */               "Variable",
/* E_VAL */               "Value",
/* E_UNDEF */             "*UNDEFINED*",
/* E_ENTER_FUN */         "Entering UserFN",
/* E_LEAVE_FUN */         "Leaving UserFN",
/* E_EXPIRED */           "Expired",
/* E_CANTFORK */          "fork() failed - can't do queued reminders",
/* E_CANTACCESS */        "Can't access file",
/* M_BAD_SYS_DATE */      "Illegal system date: Year is less than %d\n",
/* M_BAD_DB_FLAG */       "Unknown debug flag '%c'\n",
/* M_BAD_OPTION */        "Unknown option '%c'\n",
/* M_BAD_USER */          "Unknown user '%s'\n",
/* M_NO_CHG_GID */        "Could not change gid to %d\n",
/* M_NO_CHG_UID */        "Could not change uid to %d\n",
/* M_NOMEM_ENV */         "Out of memory for environment\n",
/* E_MISS_EQ */           "Missing '=' sign",
/* E_MISS_VAR */          "Missing variable name",
/* E_MISS_EXPR */         "Missing expression",
/* M_CANTSET_ACCESS */    "Can't reset access date of %s\n",
/* M_I_OPTION */          "Remind: '-i' option: %s\n",
/* E_NOREMINDERS */       "No reminders.",
/* M_QUEUED */            "%d reminder(s) queued for later today.\n",
/* E_EXPECTING_NUMBER */  "Expecting number",
/* M_BAD_WARN_FUNC */     "Undefined WARN function",
/* E_CANT_CONVERT_TZ */   "Can't convert between time zones",
/* E_NO_MATCHING_REMS */  "No files matching *.rem",
/* E_STRING_TOO_LONG */   "String too long",
/* E_TIME_TWICE */        "Time specified twice",
/* E_DURATION_NO_AT */    "Cannot specify DURATION without specifying AT",
/* E_EXPECTING_WEEKDAY */ "Expecting weekday name",
/* E_REPEATED_ARG */      "Duplicate argument name",
/* E_EXPR_DISABLED */     "Expression evaluation is disabled",
/* E_TIME_EXCEEDED */     "Time limit for expression evaluation exceeded",
}
#endif /* MK_GLOBALS */
;
#endif /* L_ERR_OVERRIDE */
