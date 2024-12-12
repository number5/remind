/***************************************************************/
/*                                                             */
/*  PROTOS.H                                                   */
/*                                                             */
/*  Function Prototypes.                                       */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/* Suppress unused variable warnings */
#define UNUSED(x) (void) x

#ifdef HAVE_STRDUP
#define StrDup strdup
#endif

#ifdef HAVE_STRNCASECMP
#define StrinCmp strncasecmp
#endif

#ifdef HAVE_STRCASECMP
#define StrCmpi strcasecmp
#endif

/* Define a string assignment macro - be careful!!! */
#define STRSET(x, str) { if (x) free(x); (x) = StrDup(str); }

/* Define a general malloc routine for creating pointers to objects */
#define NEW(type) (malloc(sizeof(type)))

/* Characters to ignore */
#define isempty(c) (isspace(c) || ((c) == '\\'))

#define IsServerMode() (Daemon < 0)
#define ShouldFork (!DontFork)

#include "dynbuf.h"
#include <ctype.h>

int CallUserFunc (char const *name, int nargs, ParsePtr p);
int DoFset (ParsePtr p);
int DoFunset (ParsePtr p);
int DoFrename (ParsePtr p);
void UnsetAllUserFuncs(void);
void ProduceCalendar (void);
char const *SimpleTime (int tim);
char const *CalendarTime (int tim, int duration);
int DoRem (ParsePtr p);
int DoFlush (ParsePtr p);
void DoExit (ParsePtr p);
int ParseRem (ParsePtr s, Trigger *trig, TimeTrig *tim);
int TriggerReminder (ParsePtr p, Trigger *t, TimeTrig *tim, int dse, int is_queued, DynamicBuffer *output);
int ShouldTriggerReminder (Trigger *t, TimeTrig *tim, int dse, int *err);
int DoSubst (ParsePtr p, DynamicBuffer *dbuf, Trigger *t, TimeTrig *tt, int dse, int mode);
int DoSubstFromString (char const *source, DynamicBuffer *dbuf, int dse, int tim);
int ParseLiteralDateOrTime (char const **s, int *dse, int *tim);
int ParseLiteralTime (char const **s, int *tim);
expr_node *parse_expression(char const **e, int *r, Var *locals);

int evaluate_expression(expr_node *node, Value *locals, Value *ans, int *nonconst);
int evaluate_expr_node(expr_node *node, Value *locals, Value *ans, int *nonconst);
int truthy(Value const *v);

void print_expr_tree(expr_node *node, FILE *fp);
void unlimit_execution_time(void);
expr_node *free_expr_tree(expr_node *node);
int EvalExpr (char const **e, Value *v, ParsePtr p);
int DoCoerce (char type, Value *v);
char const *PrintValue  (Value *v, FILE *fp);
int CopyValue (Value *dest, const Value *src);
int ReadLine (void);
int OpenFile (char const *fname);
int DoInclude (ParsePtr p, enum TokTypes tok);
int DoIncludeCmd (ParsePtr p);
int IncludeFile (char const *fname);
int GetAccessDate (char const *file);
int SetAccessDate (char const *fname, int dse);
int TopLevel (void);
int CallFunc (BuiltinFunc *f, int nargs);
void InitRemind (int argc, char const *argv[]);
void Usage (void);
int DSE (int year, int month, int day);
void FromDSE (int dse, int *y, int *m, int *d);
int JulianToGregorianOffset(int y, int m);
int ParseChar (ParsePtr p, int *err, int peek);
int ParseToken (ParsePtr p, DynamicBuffer *dbuf);
int ParseQuotedString (ParsePtr p, DynamicBuffer *dbuf);
int ParseIdentifier (ParsePtr p, DynamicBuffer *dbuf);
expr_node * ParseExpr(ParsePtr p, int *r);
void print_expr_nodes_stats(void);
int EvaluateExpr (ParsePtr p, Value *v);
int FnPopValStack (Value *val);
void Eprint (char const *fmt, ...);
void Wprint (char const *fmt, ...);
void OutputLine (FILE *fp);
void CreateParser (char const *s, ParsePtr p);
void DestroyParser (ParsePtr p);
int PushToken (char const *tok, ParsePtr p);
int SystemTime (int realtime);
int MinutesPastMidnight (int realtime);
int SystemDate (int *y, int *m, int *d);
int DoIf (ParsePtr p);
int DoElse (ParsePtr p);
int DoEndif (ParsePtr p);
int DoIfTrig (ParsePtr p);
int ShouldIgnoreLine (void);
int VerifyEoln (ParsePtr p);
int DoDebug (ParsePtr p);
int DoBanner (ParsePtr p);
int DoRun (ParsePtr p);
int DoExpr (ParsePtr p);
int DoTranslate (ParsePtr p);
int InsertTranslation(char const *orig, char const *translated);
void DumpTranslationTable(FILE *fp, int json);
int DoErrMsg (ParsePtr p);
int ClearGlobalOmits (void);
int DoClear (ParsePtr p);
int DestroyOmitContexts (int print_unmatched);
int PushOmitContext (ParsePtr p);
int PopOmitContext (ParsePtr p);
int IsOmitted (int dse, int localomit, char const *omitfunc, int *omit);
int DoOmit (ParsePtr p);
int QueueReminder (ParsePtr p, Trigger *trig, TimeTrig *tim, char const *sched);
void HandleQueuedReminders (void);
char const *FindInitialToken (Token *tok, char const *s);
void FindToken (char const *s, Token *tok);
void FindNumericToken (char const *s, Token *t);
int ComputeTrigger (int today, Trigger *trig, TimeTrig *tim, int *err, int save_in_globals);
int ComputeTriggerNoAdjustDuration (int today, Trigger *trig, TimeTrig *tim, int *err, int save_in_globals, int duration_days);
int AdjustTriggerForDuration(int today, int r, Trigger *trig, TimeTrig *tim, int save_in_globals);
char *StrnCpy (char *dest, char const *source, int n);

#ifndef HAVE_STRNCASECMP
int StrinCmp (char const *s1, char const *s2, int n);
#endif

#ifndef HAVE_STRDUP
char *StrDup (char const *s);
#endif

#ifndef HAVE_STRCASECMP
int StrCmpi (char const *s1, char const *s2);
#endif

void strtolower(char *s);

Var *FindVar (char const *str, int create);
SysVar *FindSysVar (char const *name);
int DeleteVar (char const *str);
int SetVar (char const *str, Value const *val);
int GetVarValue (char const *str, Value *val);
int DoSet  (Parser *p);
int DoUnset  (Parser *p);
int DoDump (ParsePtr p);
void DumpVarTable (void);
void DestroyVars (int all);
int PreserveVar (char const *name);
int DoPreserve  (Parser *p);
int DoSatRemind (Trigger *trig, TimeTrig *tt, ParsePtr p);
int DoMsgCommand (char const *cmd, char const *msg, int is_queued);
int ParseNonSpaceChar (ParsePtr p, int *err, int peek);
unsigned int HashVal_ignorecase(char const *str);
unsigned int HashVal_preservecase(char const *str);
int DateOK (int y, int m, int d);
BuiltinFunc *FindBuiltinFunc (char const *name);
int InsertIntoSortBuffer (int dse, int tim, char const *body, int typ, int prio);
void IssueSortedReminders (void);
UserFunc *FindUserFunc(char const *name);
int UserFuncExists (char const *fn);
void DSEToHeb (int dse, int *hy, int *hm, int *hd);
int HebNameToNum (char const *mname);
char const *HebMonthName (int m, int y);
int RoshHashana (int i);
long DaysToHebYear (int y);
int DaysInHebYear (int y);
char const *DaysInHebMonths (int ylen);
int HebToDSE (int hy, int hm, int hd);
int GetValidHebDate (int yin, int min, int din, int adarbehave, int *mout, int *dout, int yahr);
int GetNextHebrewDate (int dsestart, int hm, int hd, int yahr, int adarbehave, int *ans);
int ComputeJahr (int y, int m, int d, int *ans);
int GetSysVar (char const *name, Value *val);
int SetSysVar (char const *name, Value *val);
void DumpSysVarByName (char const *name);
int CalcMinsFromUTC (int dse, int tim, int *mins, int *isdst);
void FillParagraph (char const *s, DynamicBuffer *output);
void LocalToUTC (int locdate, int loctime, int *utcdate, int *utctime);
void UTCToLocal (int utcdate, int utctime, int *locdate, int *loctime);
int MoonPhase (int date, int time);
void HuntPhase (int startdate, int starttim, int phas, int *date, int *time);
int CompareRems (int dat1, int tim1, int prio1, int dat2, int tim2, int prio2, int bydate, int bytime, int byprio, int untimed_first);
void SigIntHandler (int d);
int GotSigInt (void);
void PurgeEchoLine(char const *fmt, ...);
void FreeTrig(Trigger *t);
void AppendTag(DynamicBuffer *buf, char const *s);
char const *SynthesizeTag(void);
void ClearLastTriggers(void);
void SaveLastTrigger(Trigger const *t);
void SaveLastTimeTrig(TimeTrig const *t);
void SaveAllTriggerInfo(Trigger const *t, TimeTrig const *tt, int trigdate, int trigtime, int valid);

void PerIterationInit(void);
char const *Decolorize(void);
char const *Colorize(int r, int g, int b, int bg, int clamp);
void PrintJSONString(char const *s);
void PrintJSONKeyPairInt(char const *name, int val);
void PrintJSONKeyPairString(char const *name, char const *val);
void PrintJSONKeyPairDate(char const *name, int dse);
void PrintJSONKeyPairDateTime(char const *name, int dt);
void PrintJSONKeyPairTime(char const *name, int t);
void System(char const *cmd, int queued);
int ShellEscape(char const *in, DynamicBuffer *out);
int AddGlobalOmit(int dse);
void set_lat_and_long_from_components(void);
void set_components_from_lat_and_long(void);

void DebugExitFunc(void);

int GetTerminalBackground(void);

char const *get_day_name(int wkday);
char const *get_month_name(int mon);

void set_cloexec(FILE *fp);
int push_call(char const *filename, char const *func, int lineno);
void clear_callstack(void);
int print_callstack(FILE *fp);
int have_callstack(void);
void pop_call(void);
void FixSpecialType(Trigger *trig);
void WriteJSONTrigger(Trigger const *t, int include_tags, int today);
void WriteJSONTimeTrigger(TimeTrig const *tt);
int GetOnceDate(void);
#ifdef REM_USE_WCHAR
#define _XOPEN_SOURCE 600
#include <wctype.h>
#include <wchar.h>
void PutWideChar(wchar_t const wc, DynamicBuffer *output);
#endif

/* These functions are in utils.c and are used to detect overflow
   in various arithmetic operators.  They have to be in separate
   functions with extern linkage to defeat compiler optimizations
   that would otherwise break the overflow checks. */
extern int _private_mul_overflow(int a, int b);
extern int _private_add_overflow(int a, int b);
extern int _private_sub_overflow(int a, int b);

/* Utility functions for dumping tokens */
void print_sysvar_tokens(void);
void print_builtinfunc_tokens(void);
void print_remind_tokens(void);

/* Stats for -ds output */
void get_var_hash_stats(int *total, int *maxlen, double *avglen);
void get_userfunc_hash_stats(int *total, int *maxlen, double *avglen);
void get_dedupe_hash_stats(int *total, int *maxlen, double *avglen);
void get_translation_hash_stats(int *total, int *maxlen, double *avglen);

/* Dedupe code */
int ShouldDedupe(int trigger_date, int trigger_time, char const *body);
void ClearDedupeTable(void);
void InitDedupeTable(void);

void InitVars(void);
void InitUserFunctions(void);
void ClearSysvarTranslations(void);
void PropagateTranslationToSysvar(char const *orig, char const *translated);
void InitTranslationTable(void);
char const *GetTranslatedString(char const *orig);
int GetTranslatedStringTryingVariants(char const *orig, DynamicBuffer *out);
char const *GetErr(int r);
char const *t(char const *s);
void print_escaped_string(FILE *fp, char const *s);
