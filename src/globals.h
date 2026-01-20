/***************************************************************/
/*                                                             */
/*  GLOBALS.H                                                  */
/*                                                             */
/*  This function contains declarations of global variables.   */
/*  They are instantiated in main.c by defining                */
/*  MK_GLOBALS.  Also contains useful macro definitions.       */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2026 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/


#ifdef MK_GLOBALS
#undef EXTERN
#define EXTERN
#define INIT(var, val) var = val
#else
#undef EXTERN
#define EXTERN extern
#define INIT(var, val) var
#endif

#include <signal.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
EXTERN  FILE *ErrFp;

#include "dynbuf.h"

#define MAX_TRUSTED_USERS 20

#define MINUTES_PER_DAY 1440

#define TODOS_AND_EVENTS 0
#define ONLY_TODOS       1
#define ONLY_EVENTS      2

#define DaysInYear(y) (((y) % 4) ? 365 : ((!((y) % 100) && ((y) % 400)) ? 365 : 366 ))
#define IsLeapYear(y) (((y) % 4) ? 0 : ((!((y) % 100) && ((y) % 400)) ? 0 : 1 ))
#define DaysInMonth(m, y) ((m) != 1 ? MonthDays[m] : 28 + IsLeapYear(y))

#define DestroyValue(x) do { if ((x).type == STR_TYPE && (x).v.str) { free((x).v.str); (x).v.str = NULL; } (x).type = ERR_TYPE; } while (0)

EXTERN  int     DSEToday;
EXTERN  int     RealToday;
EXTERN  int     LocalDSEToday;
EXTERN  int     CurDay;
EXTERN  int     CurMon;
EXTERN  int     CurYear;
EXTERN  int     LineNo;
EXTERN  int     LineNoStart;
EXTERN  int     FreshLine;
EXTERN  int     WarnedAboutImplicit;
EXTERN  uid_t   TrustedUsers[MAX_TRUSTED_USERS];

EXTERN  INIT(   int     JSONMode, 0);
EXTERN  INIT(   int     JSONLinesEmitted, 0);
EXTERN  INIT(   int     MaxLateMinutes, 0);
EXTERN  INIT(   int     NumTrustedUsers, 0);
EXTERN  INIT(   char    const *MsgCommand, NULL);
EXTERN  INIT(   char    const *QueuedMsgCommand, NULL);
EXTERN  INIT(   char    const *WarningLevel, NULL);
EXTERN  INIT(   int     ShowAllErrors, 0);
EXTERN  INIT(   int     DebugFlag, 0);
EXTERN  INIT(   int     DoCalendar, 0);
EXTERN  INIT(   int     DoSimpleCalendar, 0);
EXTERN  INIT(   int     DoSimpleCalDelta, 0);
EXTERN  INIT(   int     HideCompletedTodos, 0);
EXTERN  INIT(   int     DoPrefixLineNo, 0);
EXTERN  INIT(   int     MondayFirst, 0);
EXTERN  INIT(   int     AddBlankLines, 1);
EXTERN  INIT(   int     Iterations, 1);
EXTERN  INIT(   int     PsCal, 0);
EXTERN  INIT(   int     CalWidth, 80);
EXTERN  INIT(   int     CalWeeks, 0);
EXTERN  INIT(   int     CalMonths, 0);
EXTERN  INIT(   char const *CalType, "none");
EXTERN  INIT(   int     Hush, 0);
EXTERN  INIT(   int     NextMode, 0);
EXTERN  INIT(   int     TodoFilter, TODOS_AND_EVENTS);
EXTERN  INIT(   int     InfiniteDelta, 0);
EXTERN  INIT(   int     DefaultTDelta, 0);
EXTERN  INIT(   int     DefaultDelta, NO_DELTA);
EXTERN  INIT(   int     DeltaOverride, 0);
EXTERN  INIT(   int     RunDisabled, 0);
EXTERN  INIT(   int     ExpressionEvaluationDisabled, 0);
EXTERN  INIT(   int     ExpressionEvaluationTimeLimit, 0);
EXTERN  INIT(   unsigned long  ExpressionNodesEvaluated, 0);
EXTERN  INIT(   unsigned long  MaxExprNodesPerLine, 0);
EXTERN  INIT(   unsigned long  ExpressionNodesEvaluatedThisLine, 0);
EXTERN  INIT(   unsigned long  ExpressionNodeLimitPerLine, 10000000);
EXTERN  INIT(   volatile sig_atomic_t ExpressionTimeLimitExceeded, 0);
EXTERN  INIT(   int     IgnoreOnce, 0);
EXTERN  INIT(   char const *OnceFile, NULL);
EXTERN  INIT(   int     OnceDate, -1);
EXTERN  INIT(   int     ProcessedOnce, 0);
EXTERN  INIT(   int     SortByTime, SORT_NONE);
EXTERN  INIT(   int     SortByDate, SORT_NONE);
EXTERN  INIT(   int     SortByPrio, SORT_NONE);
EXTERN  INIT(   int     UntimedBeforeTimed, 0);
EXTERN  INIT(   int     DefaultPrio, NO_PRIORITY);
EXTERN  INIT(   int     SysTime, -1);
EXTERN  INIT(   int     LocalSysTime, -1);
EXTERN  INIT(   int     ParseUntriggered, 0);

EXTERN  char    const *InitialFile;
EXTERN  char    const *LocalTimeZone;
EXTERN  int     FileAccessDate;

EXTERN  INIT(   int     WeekdayOmits, 0);
EXTERN  INIT(   int     DontSuppressQuoteMarkers, 0);
EXTERN  INIT(   int     DontFork, 0);
EXTERN  INIT(   int     DontQueue, 0);
EXTERN  INIT(   int     NumQueued, 0);
EXTERN  INIT(   int     DontIssueAts, 0);
EXTERN  INIT(   int     Daemon, 0);
EXTERN  INIT(   int     DaemonJSON, 0);
EXTERN  INIT(   char    DateSep, DATESEP);
EXTERN  INIT(   char    TimeSep, TIMESEP);
EXTERN  INIT(   char    DateTimeSep, DATETIMESEP);
EXTERN  INIT(   int     DefaultColorR, -1);
EXTERN  INIT(   int     DefaultColorB, -1);
EXTERN  INIT(   int     DefaultColorG, -1);
EXTERN  INIT(   int     SynthesizeTags, 0);
EXTERN  INIT(   int     ScFormat, SC_AMPM);
EXTERN  INIT(   int     MaxSatIter, 10000);
EXTERN  INIT(   int     MaxStringLen, MAX_STR_LEN);
EXTERN  INIT(   int     UseStdin, 0);
EXTERN  INIT(   int     PurgeMode, 0);
EXTERN  INIT(   int     PurgeIncludeDepth, 0);
EXTERN  INIT(   FILE    *PurgeFP,  NULL);
EXTERN  INIT(   int     LastTrigValid, 0);
EXTERN  Trigger  LastTrigger;
EXTERN  TimeTrig LastTimeTrig;
EXTERN  INIT(   int     LastTriggerDate, 0);
EXTERN  INIT(   int     LastTriggerTime, NO_TIME);
EXTERN  INIT(   int     ShouldCache, 0);
EXTERN  char const   *CurLine;
EXTERN  INIT(   int     NumTriggered, 0);
EXTERN  INIT(   int     DidMsgReminder, 0);
EXTERN  int ArgC;
EXTERN  char const **ArgV;
EXTERN  INIT(   int     CalLines, CAL_LINES);
EXTERN  INIT(   int     CalPad, 1);
EXTERN  INIT(   int     CalSepLine, 1);
EXTERN  INIT(   int     UseVTChars, 0);
EXTERN  INIT(   int     UseBGVTColors, 0);
EXTERN  INIT(   int     UseUTF8Chars, 0);
EXTERN  INIT(   int     UseVTColors, 0);
EXTERN  INIT(   int     Use256Colors, 0);
EXTERN  INIT(   int     UseTrueColors, 0);
EXTERN  INIT(   int     TerminalBackground, TERMINAL_BACKGROUND_UNKNOWN);
EXTERN  INIT(   int     DedupeReminders, 0);

/* Suppress ALL error output during a catch() */
EXTERN  INIT(   int     SuppressErrorOutputInCatch, 0);

/* Latitude and longitude */
EXTERN  INIT(   int       LatDeg, 0);
EXTERN  INIT(   int       LatMin, 0);
EXTERN  INIT(   int       LatSec, 0);
EXTERN  INIT(   int       LongDeg, 0);
EXTERN  INIT(   int       LongMin, 0);
EXTERN  INIT(   int       LongSec, 0);
EXTERN  INIT(   double    Longitude, DEFAULT_LONGITUDE);
EXTERN  INIT(   double    Latitude, DEFAULT_LATITUDE);

EXTERN  INIT(   char      *Location, LOCATION);

/* Support hyperlinks in terminal emulators?
   https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
*/
EXTERN  INIT(   int       TerminalHyperlinks, -1);
/* UTC calculation stuff */
EXTERN  INIT(   int       MinsFromUTC, 0);
EXTERN  INIT(   int       CalculateUTC, 1);
EXTERN  INIT(   int       FoldYear, 0);

/* Parameters for formatting MSGF reminders */
EXTERN  INIT(   int       FormWidth, 72);
EXTERN  INIT(   int       FirstIndent, 0);
EXTERN  INIT(   int       SubsIndent, 0);
EXTERN  INIT(   char      *EndSent, ".?!");
EXTERN  INIT(   char      *EndSentIg, "\"')]}>");

EXTERN DynamicBuffer Banner;
EXTERN DynamicBuffer LineBuffer;
EXTERN DynamicBuffer ExprBuf;

/* User-func recursion level */
EXTERN  INIT(   unsigned int FuncRecursionLevel, 0);

/* Suppress warnings about implicit REM and MSG */
EXTERN  INIT(   int SuppressImplicitRemWarnings, 0);

/* Test mode - used by the acceptance tests */
EXTERN  INIT(   int TestMode, 0);

extern int NumFullOmits, NumPartialOmits;

/* List of months */
EXTERN  char    *MonthName[]
#ifdef MK_GLOBALS
= {"January", "February", "March", "April", "May", "June",
   "July", "August", "September", "October", "November", "December"}
#endif
;

EXTERN  char    *DayName[]
#ifdef MK_GLOBALS
= {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
   "Saturday", "Sunday"}
#endif
;

EXTERN  int     MonthDays[]
#ifdef MK_GLOBALS
= {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
#endif
;

/* The first day of each month expressed as number of days after Jan 1.
   Second row is for leap years. */

EXTERN  int     MonthIndex[2][12]
#ifdef MK_GLOBALS
= {
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 },
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335 }
}
#endif
;

EXTERN char *DynamicHplu
#ifdef MK_GLOBALS
= "s"
#endif
;

EXTERN char *DynamicMplu
#ifdef MK_GLOBALS
= "s"
#endif
;

#define XSTR(x) #x
#define STRSYSDIR(x) XSTR(x)

EXTERN char *SysDir
#ifdef MK_GLOBALS
= STRSYSDIR(SYSDIR)
#endif
;

EXTERN int SuppressLRM
#ifdef MK_GLOBALS
= 0
#endif
;

/* Translatable messages */
extern char const *translatables[];

/* Array for "folding" years to similar years */
extern int FoldArray[2][7];
