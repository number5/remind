/***************************************************************/
/*                                                             */
/*  PROTOS.H                                                   */
/*                                                             */
/*  Function Prototypes.                                       */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2022 by Dianne Skoll                    */
/*                                                             */
/***************************************************************/

/* Suppress unused variable warnings */
#define UNUSED(x) (void) x

/* Define a string assignment macro - be careful!!! */
#define STRSET(x, str) { if (x) free(x); (x) = StrDup(str); }

/* Define a general malloc routine for creating pointers to objects */
#define NEW(type) (malloc(sizeof(type)))

/* Characters to ignore */
#define isempty(c) (isspace(c) || ((c) == '\\'))

#include "dynbuf.h"
#include <ctype.h>

int CallUserFunc (char const *name, int nargs, ParsePtr p);
int DoFset (ParsePtr p);
void ProduceCalendar (void);
char const *SimpleTime (int tim);
char const *CalendarTime (int tim, int duration);
int DoRem (ParsePtr p);
int DoFlush (ParsePtr p);
void DoExit (ParsePtr p);
int ParseRem (ParsePtr s, Trigger *trig, TimeTrig *tim, int save_in_globals);
int TriggerReminder (ParsePtr p, Trigger *t, TimeTrig *tim, int jul);
int ShouldTriggerReminder (Trigger *t, TimeTrig *tim, int jul, int *err);
int DoSubst (ParsePtr p, DynamicBuffer *dbuf, Trigger *t, TimeTrig *tt, int jul, int mode);
int DoSubstFromString (char const *source, DynamicBuffer *dbuf, int jul, int tim);
int ParseLiteralDate (char const **s, int *jul, int *tim);
int ParseLiteralTime (char const **s, int *tim);
int EvalExpr (char const **e, Value *v, ParsePtr p);
int DoCoerce (char type, Value *v);
void PrintValue  (Value *v, FILE *fp);
int CopyValue (Value *dest, const Value *src);
int ReadLine (void);
int OpenFile (char const *fname);
int DoInclude (ParsePtr p, enum TokTypes tok);
int DoIncludeCmd (ParsePtr p);
int IncludeFile (char const *fname);
int GetAccessDate (char const *file);
int SetAccessDate (char const *fname, int jul);
int TopLevel (void);
int CallFunc (BuiltinFunc *f, int nargs);
void InitRemind (int argc, char const *argv[]);
void Usage (void);
int Julian (int year, int month, int day);
void FromJulian (int jul, int *y, int *m, int *d);
int ParseChar (ParsePtr p, int *err, int peek);
int ParseToken (ParsePtr p, DynamicBuffer *dbuf);
int ParseIdentifier (ParsePtr p, DynamicBuffer *dbuf);
int EvaluateExpr (ParsePtr p, Value *v);
int Evaluate (char const **s, Var *locals, ParsePtr p);
int FnPopValStack (Value *val);
void Eprint (char const *fmt, ...);
void Wprint (char const *fmt, ...);
void OutputLine (FILE *fp);
void CreateParser (char const *s, ParsePtr p);
void DestroyParser (ParsePtr p);
int PushToken (char const *tok, ParsePtr p);
long SystemTime (int realtime);
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
int DoErrMsg (ParsePtr p);
int ClearGlobalOmits (void);
int DoClear (ParsePtr p);
int DestroyOmitContexts (void);
int PushOmitContext (ParsePtr p);
int PopOmitContext (ParsePtr p);
int IsOmitted (int jul, int localomit, char const *omitfunc, int *omit);
int DoOmit (ParsePtr p);
int QueueReminder (ParsePtr p, Trigger *trig, TimeTrig *tim, char const *sched);
void HandleQueuedReminders (void);
char const *FindInitialToken (Token *tok, char const *s);
void FindToken (char const *s, Token *tok);
void FindNumericToken (char const *s, Token *t);
int ComputeTrigger (int today, Trigger *trig, TimeTrig *tim, int *err, int save_in_globals);
int ComputeTriggerNoAdjustDuration (int today, Trigger *trig, TimeTrig *tim, int *err, int save_in_globals, int duration_days);
int AdjustTriggerForDuration(int today, int r, Trigger *trig, TimeTrig *tim, int save_in_globals);
int ComputeScanStart(int today, Trigger *trig, TimeTrig *tt);
char *StrnCpy (char *dest, char const *source, int n);
int StrMatch (char const *s1, char const *s2, int n);
int StrinCmp (char const *s1, char const *s2, int n);
char *StrDup (char const *s);
int StrCmpi (char const *s1, char const *s2);
Var *FindVar (char const *str, int create);
int DeleteVar (char const *str);
int SetVar (char const *str, Value *val);
int GetVarValue (char const *str, Value *val, Var *locals, ParsePtr p);
int DoSet  (Parser *p);
int DoUnset  (Parser *p);
int DoDump (ParsePtr p);
void DumpVarTable (void);
void DestroyVars (int all);
int PreserveVar (char const *name);
int DoPreserve  (Parser *p);
int DoSatRemind (Trigger *trig, TimeTrig *tt, ParsePtr p);
int DoMsgCommand (char const *cmd, char const *msg);
int ParseNonSpaceChar (ParsePtr p, int *err, int peek);
unsigned int HashVal (char const *str);
int DateOK (int y, int m, int d);
Operator *FindOperator (char const *name, Operator where[], int num);
BuiltinFunc *FindFunc (char const *name, BuiltinFunc where[], int num);
int InsertIntoSortBuffer (int jul, int tim, char const *body, int typ, int prio);
void IssueSortedReminders (void);
int UserFuncExists (char const *fn);
void JulToHeb (int jul, int *hy, int *hm, int *hd);
int HebNameToNum (char const *mname);
char const *HebMonthName (int m, int y);
int RoshHashana (int i);
long DaysToHebYear (int y);
int DaysInHebYear (int y);
char const *DaysInHebMonths (int ylen);
int HebToJul (int hy, int hm, int hd);
int GetValidHebDate (int yin, int min, int din, int adarbehave, int *mout, int *dout, int yahr);
int GetNextHebrewDate (int julstart, int hm, int hd, int yahr, int adarbehave, int *ans);
int ComputeJahr (int y, int m, int d, int *ans);
int GetSysVar (char const *name, Value *val);
int SetSysVar (char const *name, Value *val);
void DumpSysVarByName (char const *name);
int CalcMinsFromUTC (int jul, int tim, int *mins, int *isdst);
void FillParagraph (char const *s);
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
void PrintJSONKeyPairDate(char const *name, int jul);
void PrintJSONKeyPairDateTime(char const *name, int dt);
void PrintJSONKeyPairTime(char const *name, int t);
void System(char const *cmd);
int ShellEscape(char const *in, DynamicBuffer *out);
int AddGlobalOmit(int jul);
void set_lat_and_long_from_components(void);
void set_components_from_lat_and_long(void);

char const *get_day_name(int wkday);
char const *get_month_name(int mon);

int push_call(char const *filename, char const *func, int lineno);
void clear_callstack(void);
int have_callstack(void);
int print_callstack(FILE *fp);
void pop_call(void);
#ifdef REM_USE_WCHAR
#define _XOPEN_SOURCE 600
#include <wctype.h>
#include <wchar.h>
void PutWideChar(wchar_t const wc);
#endif
