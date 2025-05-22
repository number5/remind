/***************************************************************/
/*                                                             */
/*  MAIN.C                                                     */
/*                                                             */
/*  Main program loop, as well as miscellaneous conversion     */
/*  routines, etc.                                             */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#define _XOPEN_SOURCE 600
#include "config.h"

#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <stdarg.h>
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <ctype.h>
#if defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#endif
#include <time.h>

#include <sys/types.h>
#ifdef REM_USE_WCHAR
#include <wctype.h>
#include <wchar.h>
#endif

#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"

static void DoReminders(void);

/* Macro for simplifying common block so as not to litter code */
#define OUTPUT(c) do { if (output) { DBufPutc(output, c); } else { putchar(c); } } while(0)

void
exitfunc(void)
{
    /* Kill any execution-time-limiter process */
    unlimit_execution_time();

    if (DebugFlag & DB_HASHSTATS) {
        fflush(stdout);
        fflush(ErrFp);
        fprintf(ErrFp, "Variable hash table statistics:\n");
        dump_var_hash_stats();

        fprintf(ErrFp, "Function hash table statistics:\n");
        dump_userfunc_hash_stats();

        fprintf(ErrFp, "Dedupe hash table statistics:\n");
        dump_dedupe_hash_stats();

        fprintf(ErrFp, "Translation hash table statistics:\n");
        dump_translation_hash_stats();

        UnsetAllUserFuncs();
        print_expr_nodes_stats();
    }
}

static void sigalrm(int sig)
{
    UNUSED(sig);
    if (ExpressionEvaluationTimeLimit) {
        ExpressionTimeLimitExceeded = 1;
    }
}

static void sigxcpu(int sig)
{

    UNUSED(sig);
    int r = write(STDERR_FILENO, "\n\nmax-execution-time exceeded.\n\n", 32);

    /* Pretend to use r to avoid compiler warning */
    /* cppcheck-suppress duplicateExpression */
    /* cppcheck-suppress knownArgument */
    _exit(1 + (r-r));
}

/***************************************************************/
/***************************************************************/
/**                                                           **/
/**  Main Program Loop                                        **/
/**                                                           **/
/***************************************************************/
/***************************************************************/
int main(int argc, char *argv[])
{
    int pid;

    struct sigaction act;

#ifdef HAVE_SETLOCALE
    setlocale(LC_ALL, "");
#endif

    /* The very first thing to do is to set up ErrFp to be stderr */
    ErrFp = stderr;

    /* Set up global vars */
    ArgC = argc;
    ArgV = (char const **) argv;

    InitRemind(argc, (char const **) argv);

    act.sa_handler = sigalrm;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &act, NULL) < 0) {
        fprintf(ErrFp, "%s: sigaction() failed: %s\n",
                argv[0], strerror(errno));
        exit(1);
    }

    act.sa_handler = sigxcpu;
    act.sa_flags = SA_RESTART;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGXCPU, &act, NULL) < 0) {
        fprintf(ErrFp, "%s: sigaction() failed: %s\n",
                argv[0], strerror(errno));
        exit(1);
    }

    DBufInit(&(LastTrigger.tags));
    LastTrigger.infos = NULL;
    ClearLastTriggers();

    atexit(exitfunc);

    if (DoCalendar || (DoSimpleCalendar && (!NextMode || PsCal))) {
        ProduceCalendar();
        return 0;
    }

    /* Are we purging old reminders?  Then just run through the loop once! */
    if (PurgeMode) {
        DoReminders();
        return 0;
    }

    /* Not doing a calendar.  Do the regular remind loop */
    ShouldCache = (Iterations > 1);

    while (Iterations--) {
        PerIterationInit();
        DoReminders();

        if (DebugFlag & DB_DUMP_VARS) {
            DumpVarTable(0);
            DumpSysVarByName(NULL);
        }

        if (!Hush) {
            if (DestroyOmitContexts(1))
                Eprint("%s", GetErr(E_PUSH_NOPOP));
            if (!Daemon && !NextMode && !NumTriggered && !NumQueued) {
                printf("%s\n", GetErr(E_NOREMINDERS));
            } else if (!Daemon && !NextMode && !NumTriggered) {
                printf(GetErr(M_QUEUED), NumQueued);
                printf("\n");
            }
        }

        /* If there are sorted reminders, handle them */
        if (SortByDate) IssueSortedReminders();

        /* If there are any background reminders queued up, handle them */
        if (NumQueued || Daemon) {

            if (DontFork) {
                HandleQueuedReminders();
                return 0;
            } else {
                pid = fork();
                if (pid == 0) {
                    HandleQueuedReminders();
                    return 0;
                }
                if (pid == -1) {
                    fprintf(ErrFp, "%s", GetErr(E_CANTFORK));
                    return 1;
                }
            }
        }
        if (Iterations) {
            DSEToday++;
        }
    }
    return 0;
}

void PurgeEchoLine(char const *fmt, ...)
{
    va_list argptr;
    va_start(argptr, fmt);
    if (PurgeFP != NULL) {
        (void) vfprintf(PurgeFP, fmt, argptr);
    }
    va_end(argptr);

}

void
PerIterationInit(void)
{
    ClearGlobalOmits();
    DestroyOmitContexts(1);
    DestroyVars(0);
    DefaultColorR = -1;
    DefaultColorG = -1;
    DefaultColorB = -1;
    NumTriggered = 0;
    ClearLastTriggers();
    ClearDedupeTable();
}

/***************************************************************/
/*                                                             */
/*  DoReminders                                                */
/*                                                             */
/*  The normal case - we're not doing a calendar.              */
/*                                                             */
/***************************************************************/
static void DoReminders(void)
{
    int r;
    Token tok;
    char const *s;
    Parser p;
    int purge_handled;

    DidMsgReminder = 0;

    if (!UseStdin) {
        FileAccessDate = GetAccessDate(InitialFile);
    } else {
        FileAccessDate = DSEToday - 1;
        if (FileAccessDate < 0) FileAccessDate = 0;
    }

    if (FileAccessDate < 0) {
        fprintf(ErrFp, "%s: `%s': %s.\n", GetErr(E_CANTACCESS), InitialFile, strerror(errno));
        exit(EXIT_FAILURE);
    }

    r=IncludeFile(InitialFile);
    if (r) {
        fprintf(ErrFp, "%s %s: %s\n", GetErr(E_ERR_READING),
                InitialFile, GetErr(r));
        exit(EXIT_FAILURE);
    }

    while(1) {
        r = ReadLine();
        if (r == E_EOF) return;
        if (r) {
            Eprint("%s: %s", GetErr(E_ERR_READING), GetErr(r));
            exit(EXIT_FAILURE);
        }
        s = FindInitialToken(&tok, CurLine);

        /* Should we ignore it? */
        if (tok.type != T_If &&
            tok.type != T_Else &&
            tok.type != T_EndIf &&
            tok.type != T_IfTrig &&
            tok.type != T_Set &&
            tok.type != T_Fset &&
            should_ignore_line())
        {
            /*** IGNORE THE LINE ***/
            if (PurgeMode) {
                if (strncmp(CurLine, "#!P", 3)) {
                    PurgeEchoLine("%s\n", CurLine);
                }
            }
        }
        else {
            purge_handled = 0;
            /* Create a parser to parse the line */
            CreateParser(s, &p);
            switch(tok.type) {

            case T_Empty:
            case T_Comment:
                if (!strncmp(CurLine, "#!P", 3)) {
                    purge_handled = 1;
                }
                break;

            case T_Rem:     r=DoRem(&p); purge_handled = 1; break;
            case T_ErrMsg:  r=DoErrMsg(&p);  break;
            case T_If:      r=DoIf(&p);      break;
            case T_IfTrig:  r=DoIfTrig(&p);  break;
            case T_Else:    r=DoElse(&p);    break;
            case T_EndIf:   r=DoEndif(&p);   break;
            case T_Include:
            case T_IncludeR:
            case T_IncludeSys:
                /* In purge mode, include closes file, so we
                   need to echo it here! */
                if (PurgeMode) {
                    PurgeEchoLine("%s\n", CurLine);
                }
                r=DoInclude(&p, tok.type);
                purge_handled = 1;
                break;
            case T_IncludeCmd:
                /* In purge mode, include closes file, so we
                   need to echo it here! */
                if (PurgeMode) {
                    PurgeEchoLine("%s\n", CurLine);
                }
                r=DoIncludeCmd(&p);
                purge_handled = 1;
                break;
            case T_Exit:    DoExit(&p);      break;
            case T_Flush:   r=DoFlush(&p);   break;
            case T_Set:     r=DoSet(&p);     break;
            case T_Fset:    r=DoFset(&p);    break;
            case T_Funset:  r=DoFunset(&p);  break;
            case T_Frename:  r=DoFrename(&p);  break;
            case T_UnSet:   r=DoUnset(&p);   break;
            case T_Clr:     r=DoClear(&p);   break;
            case T_Debug:   r=DoDebug(&p);   break;
            case T_Dumpvars: r=DoDump(&p);   break;
            case T_Banner:  r=DoBanner(&p);  break;
            case T_Omit:    r=DoOmit(&p);
                if (r == E_PARSE_AS_REM) {
                    DestroyParser(&p);
                    CreateParser(s, &p);
                    r=DoRem(&p);
                    purge_handled = 1;
                }
                break;
            case T_Pop:     r=PopOmitContext(&p);     break;
            case T_Preserve: r=DoPreserve(&p);  break;
            case T_Push:    r=PushOmitContext(&p);    break;
            case T_Expr: r = DoExpr(&p); break;
            case T_Translate: r = DoTranslate(&p); break;
            case T_RemType: if (tok.val == RUN_TYPE) {
                    r=DoRun(&p);
                } else {
                    DestroyParser(&p);
                    CreateParser(CurLine, &p);
                    r=DoRem(&p);
                    purge_handled = 1;
                }
                break;


            /* If we don't recognize the command, do a REM by default, but warn */

            default:
                if (!SuppressImplicitRemWarnings) {
                    Wprint(tr("Unrecognized command; interpreting as REM"));
                    WarnedAboutImplicit = 1;
                }
                DestroyParser(&p);
                CreateParser(CurLine, &p);
                purge_handled = 1;
                r=DoRem(&p);
                break;

            }
            if (r && (!Hush || r != E_RUN_DISABLED)) {
                Eprint("%s", GetErr(r));
            }
            if (PurgeMode) {
                if (!purge_handled) {
                    PurgeEchoLine("%s\n", CurLine);
                } else {
                    if (r) {
                        if (!Hush) {
                            PurgeEchoLine("#!P! Could not parse next line: %s\n", GetErr(r));
                        }
                        PurgeEchoLine("%s\n", CurLine);
                    }
                }
            }
            /* Destroy the parser - free up resources it may be tying up */
            DestroyParser(&p);
        }
    }
}

/***************************************************************/
/*                                                             */
/*  DSE                                                        */
/*                                                             */
/*  DSE stands for "Days Since Epoch"; the Remind epoch is     */
/*  midnight on 1990-01-01                                     */
/*                                                             */
/*  Given day, month, year, return DSE date in days since      */
/*  1 January 1990.                                            */
/*                                                             */
/***************************************************************/
int DSE(int year, int month, int day)
{
    int y1 = BASE-1, y2 = year-1;

    int y4 = (y2 / 4) - (y1 / 4);  /* Correct for leap years */
    int y100 = (y2 / 100) - (y1 / 100); /* Don't count multiples of 100... */
    int y400 = (y2 / 400) - (y1 / 400); /* ... but do count multiples of 400 */

    return 365 * (year-BASE) + y4 - y100 + y400 +
        MonthIndex[IsLeapYear(year)][month] + day - 1;
}

/***************************************************************/
/*                                                             */
/*  FromDSE                                                    */
/*                                                             */
/*  Convert a DSE date to year, month, day.  You may supply    */
/*  NULL for y, m or d if you're not interested in that value  */
/*                                                             */
/***************************************************************/
void FromDSE(int dse, int *y, int *m, int *d)
{
    int try_yr = (dse / 365) + BASE;
    int try_mon = 0;
    int t;

    /* Inline code for speed... */
    int y1 = BASE-1, y2 = try_yr-1;
    int y4 = (y2 / 4) - (y1 / 4);  /* Correct for leap years */
    int y100 = (y2 / 100) - (y1 / 100); /* Don't count multiples of 100... */
    int y400 = (y2 / 400) - (y1 / 400); /* ... but do count multiples of 400 */

    int try_dse= 365 * (try_yr-BASE) + y4 - y100 + y400;

    while (try_dse > dse) {
        try_yr--;
        try_dse -= DaysInYear(try_yr);
    }
    dse -= try_dse;

    t = DaysInMonth(try_mon, try_yr);
    while (dse >= t) {
        dse -= t;
        try_mon++;
        t = DaysInMonth(try_mon, try_yr);
    }
    if (y) {
        *y = try_yr;
    }
    if (m) {
        *m = try_mon;
    }
    if (d) {
        *d = dse + 1;
    }
    return;
}

int JulianToGregorianOffset(int y, int m)
{
    int offset = 13;
    int centuries;
    int four_centuries;
    if (y >= 2100) {
        centuries = (y - 2000) / 100;
        four_centuries = (y - 2000) / 400;
        offset += centuries - four_centuries;
        if (!(y%100) && (y % 400)) {
            if (m < 2) {
                offset--;  /* Offset increments in March */
            }
        }
    }
    return offset;
}
/***************************************************************/
/*                                                             */
/*  ParseChar                                                  */
/*                                                             */
/*  Parse a character from a parse pointer.  If peek is non-   */
/*  zero, then just peek ahead; don't advance pointer.         */
/*                                                             */
/***************************************************************/
int ParseChar(ParsePtr p, int *err, int peek)
{
    Value val;
    int r;

    *err = 0;
    if (p->tokenPushed && *p->tokenPushed) {
        if (peek) return *p->tokenPushed;
        else {
            r = *p->tokenPushed++;
            if (!r) {
                DBufFree(&p->pushedToken);
                p->tokenPushed = NULL;
            }
            return r;
        }
    }

    while(1) {
        if (p->isnested) {
            if (*(p->epos)) {
                if (peek) {
                    return *(p->epos);
                } else {
                    return *(p->epos++);
                }
            }
            free((void *) p->etext);  /* End of substituted expression */
            p->etext = NULL;
            p->epos = NULL;
            p->isnested = 0;
        }
        if (!*(p->pos)) {
            return 0;
        }
        if (*p->pos != BEG_OF_EXPR || !p->allownested) {
            if (peek) {
                return *(p->pos);
            } else {
                return *(p->pos++);
            }
        }

        /* Convert [[ to just a literal [ */
        if (*p->pos == BEG_OF_EXPR && *(p->pos+1) == BEG_OF_EXPR) {
            if (peek) {
                return *(p->pos+1);
            } else {
                p->pos++;
                return *(p->pos++);
            }
        }
        p->expr_happened = 1;
        p->pos++;
        r = EvalExpr(&(p->pos), &val, p);
        if (r) {
            *err = r;
            DestroyParser(p);
            return 0;
        }
        while(*p->pos && (isempty(*p->pos))) {
            p->pos++;
        }
        if (*p->pos != END_OF_EXPR) {
            if (*p->pos) {
                *err = E_PARSE_ERR;
            } else {
                *err = E_MISS_END;
            }
            DestroyParser(p);
            DestroyValue(val);
            return 0;
        }
        p->pos++;
        r = DoCoerce(STR_TYPE, &val);
        if (r) { *err = r; return 0; }
        p->etext = val.v.str;
        val.type = ERR_TYPE; /* So it's not accidentally destroyed! */
        p->isnested = 1;
        p->epos = p->etext;
    }
}

/***************************************************************/
/*                                                             */
/*  ParseNonSpaceChar                                          */
/*                                                             */
/*  Parse the next non-space character.                        */
/*                                                             */
/***************************************************************/
int ParseNonSpaceChar(ParsePtr p, int *err, int peek)
{
    int ch;

    ch = ParseChar(p, err, 1);
    if (*err) return 0;

    while (isempty(ch)) {
        ParseChar(p, err, 0);   /* Guaranteed to work */
        ch = ParseChar(p, err, 1);
        if (*err) return 0;
    }
    if (!peek) ch = ParseChar(p, err, 0);  /* Guaranteed to work */
    return ch;
}

/***************************************************************/
/*                                                             */
/*  ParseTokenOrQuotedString                                   */
/*                                                             */
/*  Parse either a token or a double-quote-delimited string.   */
/*                                                             */
/***************************************************************/
int ParseTokenOrQuotedString(ParsePtr p, DynamicBuffer *dbuf)
{
    int c, err;
    c = ParseNonSpaceChar(p, &err, 1);
    if (err) return err;
    if (c != '"') {
        return ParseToken(p, dbuf);
    }
    return ParseQuotedString(p, dbuf);
}

/***************************************************************/
/*                                                             */
/*  ParseQuotedString                                          */
/*                                                             */
/*  Parse a double-quote-delimited string.                     */
/*                                                             */
/***************************************************************/
int ParseQuotedString(ParsePtr p, DynamicBuffer *dbuf)
{
    int c, err, c2;
    char hexbuf[3];

    DBufFree(dbuf);
    c = ParseNonSpaceChar(p, &err, 0);
    if (err) return err;
    if (!c) {
        return E_EOLN;
    }
    if (c != '"') {
        return E_MISS_QUOTE;
    }
    c = ParseChar(p, &err, 0);
    if (err) {
        DBufFree(dbuf);
        return err;
    }
    while (c != 0 && c != '"') {
        if (c == '\\') {
            c = ParseChar(p, &err, 0);
            if (err) {
                DBufFree(dbuf);
                return err;
            }
            switch(c) {
            case 'a':
                err = DBufPutc(dbuf, '\a');
                break;
            case 'b':
                err = DBufPutc(dbuf, '\b');
                break;
            case 'f':
                err = DBufPutc(dbuf, '\f');
                break;
            case 'n':
                err = DBufPutc(dbuf, '\n');
                break;
            case 'r':
                err = DBufPutc(dbuf, '\r');
                break;
            case 't':
                err = DBufPutc(dbuf, '\t');
                break;
            case 'v':
                err = DBufPutc(dbuf, '\v');
                break;
            case 'x':
                /* \x Followed by one or two hex digits */
                c2 = ParseChar(p, &err, 1);
                if (err) break;
                if (!isxdigit(c2)) {
                    err = DBufPutc(dbuf, c);
                    break;
                }
                hexbuf[0] = c2;
                hexbuf[1] = 0;
                c2 = ParseChar(p, &err, 0);
                if (err) break;
                c2 = ParseChar(p, &err, 1);
                if (err) break;
                if (isxdigit(c2)) {
                    hexbuf[1] = c2;
                    hexbuf[2] = 0;
                    c2 = ParseChar(p, &err, 0);
                    if (err) break;
                }
                c2 = (int) strtol(hexbuf, NULL, 16);
                if (!c2) {
                    Eprint(tr("\\x00 is not a valid escape sequence"));
                    err = E_PARSE_ERR;
                } else {
                    err = DBufPutc(dbuf, c2);
                }
                break;
            default:
                err = DBufPutc(dbuf, c);
            }
        } else {
            err = DBufPutc(dbuf, c);
        }
        if (err) {
            DBufFree(dbuf);
            return err;
        }
        c = ParseChar(p, &err, 0);
        if (err) {
            DBufFree(dbuf);
            return err;
        }
    }
    if (c != '"') {
        DBufFree(dbuf);
        return E_MISS_QUOTE;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  ParseToken                                                 */
/*                                                             */
/*  Parse a token delimited by whitespace.                     */
/*                                                             */
/***************************************************************/
int ParseToken(ParsePtr p, DynamicBuffer *dbuf)
{
    int c, err;

    DBufFree(dbuf);

    c = ParseChar(p, &err, 0);
    if (err) return err;
    while (c && isempty(c)) {
        c = ParseChar(p, &err, 0);
        if (err) return err;
    }
    if (!c) return OK;
    while (c && !isempty(c)) {
        if (DBufPutc(dbuf, c) != OK) {
            DBufFree(dbuf);
            return E_NO_MEM;
        }
        c = ParseChar(p, &err, 0);
        if (err) {
            DBufFree(dbuf);
            return err;
        }
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  ParseIdentifier                                            */
/*                                                             */
/*  Parse a valid identifier - ie, alpha or underscore         */
/*  followed by alphanum.  Return E_BAD_ID if identifier is    */
/*  invalid.                                                   */
/*                                                             */
/***************************************************************/
int ParseIdentifier(ParsePtr p, DynamicBuffer *dbuf)
{
    int c, err;

    DBufFree(dbuf);

    c = ParseChar(p, &err, 0);
    if (err) return err;
    while (c && isempty(c)) {
        c = ParseChar(p, &err, 0);
        if (err) return err;
    }
    if (!c) return E_EOLN;
    if (c != '$' && c != '_' && !isalpha(c)) return E_BAD_ID;
    if (DBufPutc(dbuf, c) != OK) {
        DBufFree(dbuf);
        return E_NO_MEM;
    }

    while (1) {
        c = ParseChar(p, &err, 1);
        if (err) {
            DBufFree(dbuf);
            return err;
        }
        if (c != '_' && !isalnum(c)) return OK;
        c = ParseChar(p, &err, 0);  /* Guaranteed to work */
        if (DBufPutc(dbuf, c) != OK) {
            DBufFree(dbuf);
            return E_NO_MEM;
        }
    }
}

/***************************************************************/
/*                                                             */
/* ParseExpr                                                   */
/*                                                             */
/* We are expecting an expression here.  Parse it and return   */
/* the value node tree.                                        */
/*                                                             */
/***************************************************************/
expr_node * ParseExpr(ParsePtr p, int *r)
{

    int bracketed = 0;
    expr_node *node;

    if (p->isnested) {
        *r = E_PARSE_ERR;  /* Can't nest expressions */
        return NULL;
    }
    if (!p->pos) {
        *r = E_PARSE_ERR;      /* Missing expression */
        return NULL;
    }

    while (isempty(*p->pos)) (p->pos)++;
    if (!*(p->pos)) {
        *r = E_EOLN;
        return NULL;
    }
    if (*p->pos == BEG_OF_EXPR) {
        (p->pos)++;
        bracketed = 1;
    }
    node = parse_expression(&(p->pos), r, NULL);
    if (*r) {
        return free_expr_tree(node);
    }

    if (bracketed) {
        if (*p->pos != END_OF_EXPR) {
            if (*p->pos) {
                *r = E_PARSE_ERR;
            } else {
                *r = E_MISS_END;
            }
            return free_expr_tree(node);
        }
        (p->pos)++;
    }
    return node;
}

/***************************************************************/
/*                                                             */
/* EvaluateExpr                                                */
/*                                                             */
/* We are expecting an expression here.  Evaluate it and       */
/* return the value.                                           */
/*                                                             */
/***************************************************************/
int EvaluateExpr(ParsePtr p, Value *v)
{

    int r;
    int nonconst = 0;
    expr_node *node = ParseExpr(p, &r);

    if (r != OK) {
        return r;
    }
    if (!node) {
        return E_SWERR;
    }

    r = evaluate_expression(node, NULL, v, &nonconst);
    free_expr_tree(node);
    if (r) return r;
    if (nonconst) {
        p->nonconst_expr = 1;
    }
    return OK;
}

/***************************************************************/
/*                                                             */
/*  Wprint - print a warning message.                          */
/*                                                             */
/***************************************************************/
void Wprint(char const *fmt, ...)
{
    va_list argptr;

    if (SuppressErrorOutputInCatch) {
        return;
    }

    /* We can't use line_range because caller might have used it */
    if (FileName) {
        if (strcmp(FileName, "-")) {
            if (LineNoStart == LineNo) {
                (void) fprintf(ErrFp, "%s(%d): ", FileName, LineNo);
            } else {
                (void) fprintf(ErrFp, "%s(%d:%d): ", FileName, LineNoStart, LineNo);
            }
        } else {
            if (LineNoStart == LineNo) {
                (void) fprintf(ErrFp, "-stdin-(%d): ", LineNo);
            } else {
                (void) fprintf(ErrFp, "-stdin-(%d:%d): ", LineNoStart, LineNo);
            }
        }
    }

    va_start(argptr, fmt);
    (void) vfprintf(ErrFp, fmt, argptr);
    (void) fputc('\n', ErrFp);
    va_end(argptr);
    return;
}
/***************************************************************/
/*                                                             */
/*  Eprint - print an error message.                           */
/*                                                             */
/***************************************************************/
void Eprint(char const *fmt, ...)
{
    va_list argptr;
    char const *fname;

    if (SuppressErrorOutputInCatch) {
        return;
    }

    /* Check if more than one error msg. from this line */
    if (!FreshLine && !ShowAllErrors) return;

    if (!FileName) {
        return;
    }

    if (strcmp(FileName, "-")) {
        fname = FileName;
    } else {
        fname = "-stdin-";
    }
    if (FreshLine) {
        /* We can't use line_range because caller might have used it */
        if (LineNo == LineNoStart) {
            (void) fprintf(ErrFp, "%s(%d): ", fname, LineNo);
        } else {
            (void) fprintf(ErrFp, "%s(%d:%d): ", fname, LineNoStart, LineNo);
        }
    } else {
        fprintf(ErrFp, "       ");
    }
    va_start(argptr, fmt);
    (void) vfprintf(ErrFp, fmt, argptr);
    (void) fputc('\n', ErrFp);
    va_end(argptr);
    if (print_callstack(ErrFp)) {
        (void) fprintf(ErrFp, "\n");
    }
    if (FreshLine) {
        if (DebugFlag & DB_PRTLINE) OutputLine(ErrFp);
    }
    FreshLine = 0;
}

/***************************************************************/
/*                                                             */
/*  OutputLine                                                 */
/*                                                             */
/*  Output a line from memory buffer to a file pointer.  This  */
/*  simply involves escaping newlines.                         */
/*                                                             */
/***************************************************************/
void OutputLine(FILE *fp)
{
    char const *s = CurLine;
    char c = 0;

    while (*s) {
        if (*s == '\n') putc('\\', fp);
        putc(*s, fp);
        c = *s++;
    }
    if (c != '\n') putc('\n', fp);
}

/***************************************************************/
/*                                                             */
/*  CreateParser                                               */
/*                                                             */
/*  Create a parser given a string buffer                      */
/*                                                             */
/***************************************************************/
void CreateParser(char const *s, ParsePtr p)
{
    p->text = s;
    p->pos = s;
    p->isnested = 0;
    p->epos = NULL;
    p->etext = NULL;
    p->allownested = 1;
    p->tokenPushed = NULL;
    p->expr_happened = 0;
    p->nonconst_expr = 0;
    DBufInit(&p->pushedToken);
}

/***************************************************************/
/*                                                             */
/*  DestroyParser                                              */
/*                                                             */
/*  Destroy a parser, freeing up resources used.               */
/*                                                             */
/***************************************************************/
void DestroyParser(ParsePtr p)
{
    if (p->isnested && p->etext) {
        free((void *) p->etext);
        p->etext = NULL;
        p->isnested = 0;
    }
    DBufFree(&p->pushedToken);
}

/***************************************************************/
/*                                                             */
/*  PushToken - one level of token pushback.  This is          */
/*  on a per-parser basis.                                     */
/*                                                             */
/***************************************************************/
int PushToken(char const *tok, ParsePtr p)
{
    DBufFree(&p->pushedToken);
    if (DBufPuts(&p->pushedToken, tok) != OK ||
        DBufPutc(&p->pushedToken, ' ') != OK) {
        DBufFree(&p->pushedToken);
        return E_NO_MEM;
    }
    p->tokenPushed = DBufValue(&p->pushedToken);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  SystemTime                                                 */
/*                                                             */
/*  Return the system time in seconds past midnight            */
/*                                                             */
/***************************************************************/
int SystemTime(int realtime)
{
    time_t now;
    struct tm *t;

    if (!realtime && (SysTime != -1)) return SysTime;

    if (TestMode) {
        /* Pretend it's 7:00PM in test mode */
        return 19 * 3600;
    }

    now = time(NULL);
    t = localtime(&now);
    return t->tm_hour * 3600L + t->tm_min * 60L +
        t->tm_sec;
}

/***************************************************************/
/*                                                             */
/*  MinutesPastMidnight                                        */
/*                                                             */
/*  Return the system time in minutes past midnight            */
/*                                                             */
/***************************************************************/
int MinutesPastMidnight(int realtime)
{
    return (SystemTime(realtime) / 60);
}


/***************************************************************/
/*                                                             */
/*  SystemDate                                                 */
/*                                                             */
/*  Obtains today's date.  Returns DSE date or -1 for       */
/*  failure.  (Failure happens if sys date is before BASE      */
/*  year.)                                                     */
/*                                                             */
/***************************************************************/
int SystemDate(int *y, int *m, int *d)
{
    time_t now;
    struct tm *t;

    /* In test mode, always return 6 January 2025 */
    if (TestMode) {
        *y = 2025;
        *m = 0;
        *d = 6;
        return 12789; /* 2025-01-06 */
    }

    now = time(NULL);
    t = localtime(&now);

    *d = t->tm_mday;
    *m = t->tm_mon;
    *y = t->tm_year + 1900;

    return DSE(*y, *m, *d);
}


/***************************************************************/
/*                                                             */
/*  DoIf - handle the IF command.                              */
/*                                                             */
/***************************************************************/
int DoIf(ParsePtr p)
{
    Value v;
    int r;

    if (if_stack_full()) {
        return E_NESTED_IF;
    }

    if (should_ignore_line()) {
        push_if(1, 1);
        return OK;
    } else {
        if ( (r = EvaluateExpr(p, &v)) ) {
            Eprint("%s", GetErr(r));
            push_if(1, 0);
        } else
            if (truthy(&v)) {
                push_if(1, !p->nonconst_expr);
            } else {
                push_if(0, !p->nonconst_expr);
                if (PurgeMode && !Hush) {
                    PurgeEchoLine("%s\n", "#!P: The next IF evaluated false...");
                    PurgeEchoLine("%s\n", "#!P: REM statements in IF block not checked for purging.");
                }
            }
    }
    return VerifyEoln(p);
}


/***************************************************************/
/*                                                             */
/*  DoElse - handle the ELSE command.                          */
/*                                                             */
/***************************************************************/
int DoElse(ParsePtr p)
{
    int was_ignoring = should_ignore_line();

    int r = encounter_else();
    if (PurgeMode && should_ignore_line() && !was_ignoring && !Hush) {
        PurgeEchoLine("%s\n", "#!P: The previous IF evaluated true.");
        PurgeEchoLine("%s\n", "#!P: REM statements in ELSE block not checked for purging");
    }
    if (r != OK) {
        return r;
    }
    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  DoEndif - handle the Endif command.                        */
/*                                                             */
/***************************************************************/
int DoEndif(ParsePtr p)
{
    int r = encounter_endif();
    if (r != OK) {
        return r;
    }
    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  DoIfTrig                                                   */
/*                                                             */
/*  Handle the IFTRIG command.                                 */
/*                                                             */
/***************************************************************/
int DoIfTrig(ParsePtr p)
{
    int r, err;
    Trigger trig;
    TimeTrig tim;
    int dse;


    if (if_stack_full()) {
        return E_NESTED_IF;
    }

    if (should_ignore_line()) {
        push_if(1, 0);
        return OK;
    } else {
        if ( (r=ParseRem(p, &trig, &tim)) ) return r;
        if (trig.typ != NO_TYPE) return E_PARSE_ERR;
        dse = ComputeTrigger(trig.scanfrom, &trig, &tim, &r, 1);
        if (r) {
            if (r != E_CANT_TRIG || !trig.maybe_uncomputable) {
                if (!Hush || r != E_RUN_DISABLED) {
                    Eprint("%s", GetErr(r));
                }
            }
            push_if(0, 0);
        } else {
            if (ShouldTriggerReminder(&trig, &tim, dse, &err)) {
                push_if(1, 0);
            } else {
                push_if(0, 0);
                if (PurgeMode && !Hush) {
                    PurgeEchoLine("%s\n", "#!P: The next IFTRIG did not trigger.");
                    PurgeEchoLine("%s\n", "#!P: REM statements in IFTRIG block not checked for purging.");
                }
            }
        }
        FreeTrig(&trig);
    }
    return OK;
}


/***************************************************************/
/*                                                             */
/*  VerifyEoln                                                 */
/*                                                             */
/*  Verify that current line contains no more tokens.          */
/*                                                             */
/***************************************************************/
int VerifyEoln(ParsePtr p)
{
    int r;

    DynamicBuffer buf;
    DBufInit(&buf);

    if ( (r = ParseToken(p, &buf)) ) return r;
    if (*DBufValue(&buf) &&
        (*DBufValue(&buf) != '#') &&
        (*DBufValue(&buf) != ';')) {
        Eprint("%s: `%s'", GetErr(E_EXPECTING_EOL), DBufValue(&buf));
        DBufFree(&buf);
        return E_EXTRANEOUS_TOKEN;
    }
    DBufFree(&buf);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  DoDebug                                                    */
/*                                                             */
/*  Set the debug options under program control.               */
/*                                                             */
/***************************************************************/
int DoDebug(ParsePtr p)
{
    int err;
    int ch;
    int val=1;

    while(1) {
        ch = ParseChar(p, &err, 0);
        if (err) return err;
        switch(ch) {
        case '#':
        case ';':
        case 0:
            return OK;

        case ' ':
        case '\t':
            break;

        case '+':
            val = 1;
            break;

        case '-':
            val = 0;
            break;

        case 'e':
        case 'E':
            if (val) DebugFlag |=  DB_ECHO_LINE;
            else     DebugFlag &= ~DB_ECHO_LINE;
            break;

        case 'q':
        case 'Q':
            if (val) DebugFlag |=  DB_TRANSLATE;
            else     DebugFlag &= ~DB_TRANSLATE;
            break;

        case 's':
        case 'S':
            if (val) DebugFlag |=  DB_PARSE_EXPR;
            else     DebugFlag &= ~DB_PARSE_EXPR;
            break;

        case 'h':
        case 'H':
            if (val) DebugFlag |=  DB_HASHSTATS;
            else     DebugFlag &= ~DB_HASHSTATS;
            break;

        case 'x':
        case 'X':
            if (val) DebugFlag |=  DB_PRTEXPR;
            else     DebugFlag &= ~DB_PRTEXPR;
            break;

        case 't':
        case 'T':
            if (val) DebugFlag |=  DB_PRTTRIG;
            else     DebugFlag &= ~DB_PRTTRIG;
            break;

        case 'v':
        case 'V':
            if (val) DebugFlag |=  DB_DUMP_VARS;
            else     DebugFlag &= ~DB_DUMP_VARS;
            break;

        case 'l':
        case 'L':
            if (val) DebugFlag |=  DB_PRTLINE;
            else     DebugFlag &= ~DB_PRTLINE;
            break;

        case 'f':
        case 'F':
            if (val) DebugFlag |= DB_TRACE_FILES;
            else     DebugFlag &= ~DB_TRACE_FILES;
            break;

        case 'n':
        case 'N':
            if (val) DebugFlag |= DB_NONCONST;
            else     DebugFlag &= ~DB_NONCONST;
            break;
        default:
            Wprint(GetErr(M_BAD_DB_FLAG), ch);
            break;
        }
    }
}

/***************************************************************/
/*                                                             */
/*  DoBanner                                                   */
/*                                                             */
/*  Set the banner to be printed just before the first         */
/*  reminder is issued.                                        */
/*                                                             */
/***************************************************************/
int DoBanner(ParsePtr p)
{
    int err;
    int c;
    DynamicBuffer buf;

    DBufInit(&buf);
    c = ParseChar(p, &err, 0);
    if (err) return err;
    while (isempty(c)) {
        c = ParseChar(p, &err, 0);
        if (err) return err;
    }
    if (!c) return E_EOLN;

    while(c) {
        if (DBufPutc(&buf, c) != OK) return E_NO_MEM;
        c = ParseChar(p, &err, 0);
        if (err) {
            DBufFree(&buf);
            return err;
        }
    }
    DBufFree(&Banner);

    err = DBufPuts(&Banner, DBufValue(&buf));
    DBufFree(&buf);
    return err;
}

/***************************************************************/
/*                                                             */
/*  DoRun                                                      */
/*                                                             */
/*  Enable or disable the RUN command under program control    */
/*                                                             */
/***************************************************************/
int DoRun(ParsePtr p)
{
    int r;

    DynamicBuffer buf;
    DBufInit(&buf);

    if ( (r=ParseToken(p, &buf)) ) return r;

/* Only allow RUN ON in top-level script */
    if (! StrCmpi(DBufValue(&buf), "ON")) {
        if (TopLevel()) RunDisabled &= ~RUN_SCRIPT;
    }
/* But allow RUN OFF anywhere */
    else if (! StrCmpi(DBufValue(&buf), "OFF"))
        RunDisabled |= RUN_SCRIPT;
    else {
        DBufFree(&buf);
        return E_PARSE_ERR;
    }
    DBufFree(&buf);

    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  DoExpr                                                     */
/*                                                             */
/*  Enable or disable expression evaluation                    */
/*                                                             */
/***************************************************************/
int DoExpr(ParsePtr p)
{
    int r;

    DynamicBuffer buf;
    DBufInit(&buf);

    if ( (r=ParseToken(p, &buf)) ) return r;

/* Only allow EXPR ON in top-level script */
    if (! StrCmpi(DBufValue(&buf), "ON")) {
        if (TopLevel()) ExpressionEvaluationDisabled = 0;
    }
/* But allow EXPR OFF anywhere */
    else if (! StrCmpi(DBufValue(&buf), "OFF"))
        ExpressionEvaluationDisabled = 1;
    else {
        DBufFree(&buf);
        return E_PARSE_ERR;
    }
    DBufFree(&buf);

    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  DoFlush                                                    */
/*                                                             */
/*  Flush stdout and stderr                                    */
/*                                                             */
/***************************************************************/
int DoFlush(ParsePtr p)
{
    fflush(stdout);
    fflush(stderr);
    return VerifyEoln(p);
}

/***************************************************************/
/*                                                             */
/*  DoExit                                                     */
/*                                                             */
/*  Handle the EXIT command.                                   */
/*                                                             */
/***************************************************************/
void DoExit(ParsePtr p)
{
    int r;
    Value v;

    if (PurgeMode) return;

    r = EvaluateExpr(p, &v);
    if (r || v.type != INT_TYPE) exit(99);
    exit(v.v.val);
}

/***************************************************************/
/*                                                             */
/*  DoErrMsg                                                   */
/*                                                             */
/*  Issue an error message under program control.              */
/*                                                             */
/***************************************************************/
int DoErrMsg(ParsePtr p)
{
    TimeTrig tt;
    Trigger t;
    int r;
    char const *s;

    DynamicBuffer buf;

    if (PurgeMode) return OK;

    DBufInit(&buf);
    t.typ = MSG_TYPE;
    tt.ttime = SystemTime(0) / 60;
    if ( (r=DoSubst(p, &buf, &t, &tt, DSEToday, NORMAL_MODE)) ) {
        return r;
    }
    s = DBufValue(&buf);
    while (isempty(*s)) s++;
    fprintf(ErrFp, "%s\n", s);
    DBufFree(&buf);
    return OK;
}

/***************************************************************/
/*                                                             */
/*  CalcMinsFromUTC                                            */
/*                                                             */
/*  Attempt to calculate the minutes from UTC for a specific   */
/*  date.                                                      */
/*                                                             */
/***************************************************************/

/* The array FoldArray[2][7] contains sample years which begin
   on the specified weekday.  For example, FoldArray[0][2] is a
   non-leap year beginning on Wednesday, and FoldArray[1][5] is a
   leap year beginning on Saturday.  Used to fold back dates which
   are too high for the standard Unix representation.
   NOTE:  This implies that you cannot set BASE > 2012!!!!! */
int FoldArray[2][7] = {
    {2035, 2030, 2031, 2026, 2027, 2033, 2034},
    {2024, 2036, 2020, 2032, 2016, 2028, 2012}
};

int CalcMinsFromUTC(int dse, int tim, int *mins, int *isdst)
{

/* Convert dse and tim to an Unix tm struct */
    int yr, mon, day;
    int tdiff;
    struct tm local, utc, *temp;
    time_t loc_t, utc_t;
    int isdst_tmp;

    FromDSE(dse, &yr, &mon, &day);

/* If the year is greater than 2037, some Unix machines have problems.
   Fold it back to a "similar" year and trust that the UTC calculations
   are still valid... */
    if (yr > 2037 && (FoldYear || (sizeof(time_t) < (64/CHAR_BIT)))) {
        dse = DSE(yr, 0, 1);
        yr = FoldArray[IsLeapYear(yr)][dse%7];
    }
    local.tm_sec = 0;
    local.tm_min = tim % 60;
    local.tm_hour = tim / 60;


    local.tm_mday = day;
    local.tm_mon = mon;
    local.tm_year = yr-1900;
    local.tm_isdst = -1;  /* We don't know whether or not dst is in effect */


    /* Horrible contortions to get minutes from UTC portably */
    loc_t = mktime(&local);
    if (loc_t == -1) return 1;
    isdst_tmp = local.tm_isdst;
    local.tm_isdst = 0;
    loc_t = mktime(&local);
    if (loc_t == -1) return 1;
    temp = gmtime(&loc_t);
    utc = *temp;
    utc.tm_isdst = 0;
    utc_t = mktime(&utc);
    if (utc_t == -1) return 1;
    /* Compute difference between local time and UTC in seconds.
       Be careful, since time_t might be unsigned. */

    tdiff = (int) difftime(loc_t, utc_t);
    if (isdst_tmp) tdiff += 60*60;
    if (mins) *mins = (int)(tdiff / 60);
    if (isdst) *isdst = isdst_tmp;
    return 0;
}

static char const *OutputEscapeSequences(char const *s, int print, DynamicBuffer *output)
{
    while (*s == 0x1B && *(s+1) == '[') {
        if (print) OUTPUT(*s);
        s++;
        if (print) OUTPUT(*s);
        s++;
        while (*s && (*s < 0x40 || *s > 0x7E)) {
            if (print) OUTPUT(*s);
            s++;
        }
        if (*s) {
            if (print) OUTPUT(*s);
            s++;
        }
    }
    return s;
}

#ifdef REM_USE_WCHAR
#define ISWBLANK(c) (iswspace(c) && (c) != '\n')
static wchar_t const *OutputEscapeSequencesWS(wchar_t const *s, int print, DynamicBuffer *output)
{
    while (*s == 0x1B && *(s+1) == '[') {
        if (print) PutWideChar(*s, output);
        s++;
        if (print) PutWideChar(*s, output);
        s++;
        while (*s && (*s < 0x40 || *s > 0x7E)) {
            if (print) PutWideChar(*s, output);
            s++;
        }
        if (*s) {
            if (print) PutWideChar(*s, output);
            s++;
        }
    }
    return s;
}


static void
FillParagraphWCAux(wchar_t const *s, DynamicBuffer *output)
{
    int line = 0;
    int i, j;
    int doublespace = 1;
    int pendspace;
    int len;
    wchar_t const *t;

    int roomleft;
    /* Start formatting */
    while(1) {

        /* If it's a carriage return, output it and start new paragraph */
        if (*s == '\n') {
            OUTPUT('\n');
            s++;
            line = 0;
            while(ISWBLANK(*s)) s++;
            continue;
        }
        if (!*s) {
            return;
        }
        /* Over here, we're at the beginning of a line.  Emit the correct
           number of spaces */
        j = line ? SubsIndent : FirstIndent;
        for (i=0; i<j; i++) {
            OUTPUT(' ');
        }

        /* Calculate the amount of room left on this line */
        roomleft = FormWidth - j;
        pendspace = 0;

        /* Emit words until the next one won't fit */
        while(1) {
            while(ISWBLANK(*s)) s++;
            if (*s == '\n') break;
            while(1) {
                t = s;
                s = OutputEscapeSequencesWS(s, 1, output);
                if (s == t) break;
                while(ISWBLANK(*s)) s++;
            }
            t = s;
            len = 0;
            while(*s && !iswspace(*s)) {
                if (*s == 0x1B && *(s+1) == '[') {
                    s = OutputEscapeSequencesWS(s, 0, output);
                    continue;
                }
                len += wcwidth(*s);
                s++;
            }
            if (s == t) {
                return;
            }
            if (!pendspace || len+pendspace <= roomleft) {
                for (i=0; i<pendspace; i++) {
                    OUTPUT(' ');
                }
                while(t < s) {
                    PutWideChar(*t, output);
                    if (strchr(EndSent, *t)) doublespace = 2;
                    else if (!strchr(EndSentIg, *t)) doublespace = 1;
                    t++;
                }
            } else {
                s = t;
                OUTPUT('\n');
                line++;
                break;
            }
            roomleft -= len+doublespace;
            pendspace = doublespace;
        }
    }
}

static int
FillParagraphWC(char const *s, DynamicBuffer *output)
{
    size_t len;
    wchar_t *buf;

    len = mbstowcs(NULL, s, 0);
    if (len == (size_t) -1) return E_NO_MEM;
    buf = calloc(len+1, sizeof(wchar_t));
    if (!buf) return E_NO_MEM;
    (void) mbstowcs(buf, s, len+1);
    FillParagraphWCAux(buf, output);
    free(buf);
    return OK;
}
#endif
/***************************************************************/
/*                                                             */
/*  FillParagraph                                              */
/*                                                             */
/*  Write a string to standard output, formatting it as a      */
/*  paragraph according to the FirstIndent, FormWidth and      */
/*  SubsIndent variables.  Spaces are gobbled.  Double-spaces  */
/*  are inserted after '.', '?' and '!'.  Newlines in the      */
/*  source are treated as paragraph breaks.                    */
/*                                                             */
/***************************************************************/

/* A macro safe ONLY if used with arg with no side effects! */
#define ISBLANK(c) (isspace(c) && (c) != '\n')

void FillParagraph(char const *s, DynamicBuffer *output)
{

    int line = 0;
    int i, j;
    int doublespace = 1;
    int pendspace;
    int len;
    char const *t;

    int roomleft;
    if (!s || !*s) return;

    /* Skip leading spaces */
    while(ISBLANK(*s)) s++;
    if (!*s) return;

#ifdef REM_USE_WCHAR
    if (FillParagraphWC(s, output) == OK) {
        return;
    }
#endif

    /* Start formatting */
    while(1) {

        /* If it's a carriage return, output it and start new paragraph */
        if (*s == '\n') {
            OUTPUT('\n');
            s++;
            line = 0;
            while(ISBLANK(*s)) s++;
            continue;
        }
        if (!*s) {
            return;
        }
        /* Over here, we're at the beginning of a line.  Emit the correct
           number of spaces */
        j = line ? SubsIndent : FirstIndent;
        for (i=0; i<j; i++) {
            OUTPUT(' ');
        }

        /* Calculate the amount of room left on this line */
        roomleft = FormWidth - j;
        pendspace = 0;

        /* Emit words until the next one won't fit */
        while(1) {
            while(ISBLANK(*s)) s++;
            if (*s == '\n') break;
            while(1) {
                t = s;
                s = OutputEscapeSequences(s, 1, output);
                if (s == t) break;
                while(ISBLANK(*s)) s++;
            }
            t = s;
            len = 0;
            while(*s && !isspace(*s)) {
                if (*s == 0x1B && *(s+1) == '[') {
                    s = OutputEscapeSequences(s, 0, output);
                    continue;
                }
                s++;
                len++;
            }
            if (s == t) {
                return;
            }
            if (!pendspace || len+pendspace <= roomleft) {
                for (i=0; i<pendspace; i++) {
                    OUTPUT(' ');
                }
                while(t < s) {
                    OUTPUT(*t);
                    if (strchr(EndSent, *t)) doublespace = 2;
                    else if (!strchr(EndSentIg, *t)) doublespace = 1;
                    t++;
                }
            } else {
                s = t;
                OUTPUT('\n');
                line++;
                break;
            }
            roomleft -= len+doublespace;
            pendspace = doublespace;
        }
    }
}

/***************************************************************/
/*                                                             */
/*  LocalToUTC                                                 */
/*                                                             */
/*  Convert a local date/time to a UTC date/time.              */
/*                                                             */
/***************************************************************/
void LocalToUTC(int locdate, int loctime, int *utcdate, int *utctime)
{
    int diff;
    int dummy;

    if (!CalculateUTC || CalcMinsFromUTC(locdate, loctime, &diff, &dummy)) 
        diff=MinsFromUTC;

    loctime -= diff;
    if (loctime < 0) {
        loctime += MINUTES_PER_DAY;
        locdate--;
    } else if (loctime >= MINUTES_PER_DAY) {
        loctime -= MINUTES_PER_DAY;
        locdate++;
    }
    *utcdate = locdate;
    *utctime = loctime;
}

/***************************************************************/
/*                                                             */
/*  UTCToLocal                                                 */
/*                                                             */
/*  Convert a UTC date/time to a local date/time.              */
/*                                                             */
/***************************************************************/
void UTCToLocal(int utcdate, int utctime, int *locdate, int *loctime)
{
    int diff;
    int dummy;

    /* Hack -- not quite right when DST changes.  */
    if (!CalculateUTC || CalcMinsFromUTC(utcdate, utctime, &diff, &dummy))
        diff=MinsFromUTC;

    utctime += diff;
    if (utctime < 0) {
        utctime += MINUTES_PER_DAY;
        utcdate--;
    } else if (utctime >= MINUTES_PER_DAY) {
        utctime -= MINUTES_PER_DAY;
        utcdate++;
    }
    *locdate = utcdate;
    *loctime = utctime;
}

/***************************************************************/
/*                                                             */
/* SigIntHandler                                               */
/*                                                             */
/* For debugging purposes, when sent a SIGINT, we print the    */
/* contents of the queue.  This does NOT work when the -f      */
/* command-line flag is supplied.                              */
/*                                                             */
/***************************************************************/
static sig_atomic_t got_sigint = 0;

void
SigIntHandler(int d)
{
    UNUSED(d);
    got_sigint = 1;
}

int
GotSigInt(void)
{
    if (got_sigint) {
        got_sigint = 0;
        return 1;
    }
    return 0;
}

void
AppendTag(DynamicBuffer *buf, char const *s)
{
    if (*(DBufValue(buf))) {
        DBufPutc(buf, ',');
    }
    DBufPuts(buf, s);
}

void
FreeTrig(Trigger *t)
{
    DBufFree(&(t->tags));
    if (t->infos) {
        FreeTrigInfoChain(t->infos);
    }
    t->infos = NULL;
}

void
ClearLastTriggers(void)
{
    LastTrigger.expired = 0;
    LastTrigger.wd = NO_WD;
    LastTrigger.d = NO_DAY;
    LastTrigger.m = NO_MON;
    LastTrigger.y = NO_YR;
    LastTrigger.back = NO_BACK;
    LastTrigger.delta = NO_DELTA;
    LastTrigger.rep  = NO_REP;
    LastTrigger.localomit = NO_WD;
    LastTrigger.skip = NO_SKIP;
    LastTrigger.until = NO_UNTIL;
    LastTrigger.typ = NO_TYPE;
    LastTrigger.once = NO_ONCE;
    LastTrigger.scanfrom = NO_DATE;
    LastTrigger.from = NO_DATE;
    LastTrigger.priority = DefaultPrio;
    LastTrigger.sched[0] = 0;
    LastTrigger.warn[0] = 0;
    LastTrigger.omitfunc[0] = 0;
    LastTrigger.passthru[0] = 0;
    FreeTrig(&LastTrigger);
    LastTimeTrig.ttime = NO_TIME;
    LastTimeTrig.delta = NO_DELTA;
    LastTimeTrig.rep   = NO_REP;
    LastTimeTrig.duration = NO_TIME;
}

void
SaveAllTriggerInfo(Trigger const *t, TimeTrig const *tt, int trigdate, int trigtime, int valid)
{
    SaveLastTrigger(t);
    SaveLastTimeTrig(tt);
    LastTriggerDate = trigdate;
    LastTriggerTime = trigtime;
    LastTrigValid = valid;
}

void
SaveLastTrigger(Trigger const *t)
{
    FreeTrig(&LastTrigger);
    memcpy(&LastTrigger, t, sizeof(LastTrigger));

    /* DON'T hang on to the invalid info chain! */
    LastTrigger.infos = NULL;
    DBufInit(&(LastTrigger.tags));

    DBufPuts(&(LastTrigger.tags), DBufValue(&(t->tags)));
    TrigInfo *cur = t->infos;
    while(cur) {
        AppendTrigInfo(&LastTrigger, cur->info);
        cur = cur->next;
    }
}

void
SaveLastTimeTrig(TimeTrig const *t)
{
    memcpy(&LastTimeTrig, t, sizeof(LastTimeTrig));
}

/* Wrapper to ignore warnings about ignoring return value of system()
   Also redirects stdin and stdout to /dev/null for queued reminders */

void
System(char const *cmd, int is_queued)
{
    pid_t kid;
    int fd;
    int status;
    int do_exit = 0;
    if (is_queued && IsServerMode()) {
        do_exit = 1;
        /* Server mode... redirect stdin and stdout to /dev/null */
        kid = fork();
        if (kid == (pid_t) -1) {
            /* Fork failed... nothing we can do */
            return;
        } else if (kid == 0) {
            /* In the child */
            (void) close(STDIN_FILENO);
            (void) close(STDOUT_FILENO);
            fd = open("/dev/null", O_RDONLY);
            if (fd >= 0 && fd != STDIN_FILENO) {
                dup2(fd, STDIN_FILENO);
                close(STDIN_FILENO);
            }
            fd = open("/dev/null", O_WRONLY);
            if (fd >= 0 && fd != STDOUT_FILENO) {
                dup2(fd, STDOUT_FILENO);
                close(STDOUT_FILENO);
            }
        } else {
            /* In the parent */
            while (waitpid(kid, &status, 0) != kid) /* continue */ ;
            return;
        }
    }
    /* This is the child process or original if we never forked */
    (void) system(cmd);
    if (do_exit) {
        /* In the child process, so exit! */
        exit(0);
    }
    return;
}

char const *
get_day_name(int wkday)
{
    if (wkday < 0 || wkday > 6) {
        return "INVALID_WKDAY";
    }
    return tr(DayName[wkday]);
}

char const *
get_month_name(int mon)
{
    if (mon < 0 || mon > 11) {
        return "INVALID_MON";
    }
    return tr(MonthName[mon]);
}

static int GetOnceDateFromFile(void)
{
    FILE *fp;

    int once_date = 0;

    fp = fopen(OnceFile, "r");
    if (fp) {
        if (fscanf(fp, "%d", &once_date) != 1) {
            once_date = 0;
        }
        fclose(fp);
    }
    /* Save today to file */
    fp = fopen(OnceFile, "w");
    if (!fp) {
        Wprint(tr("Warning: Unable to save ONCE timestamp to %s: %s"),
               OnceFile, strerror(errno));
        return once_date;
    }
    fprintf(fp, "%d\n# This is a timestamp file used by Remind to track ONCE reminders.\n# Do not edit or delete it.\n", DSEToday);
    fclose(fp);
    return once_date;
}

int GetOnceDate(void)
{
    ProcessedOnce = 1;
    if (IgnoreOnce || !OnceFile || !*OnceFile) {
        return FileAccessDate;
    }
    if (OnceDate < 0) {
        OnceDate = GetOnceDateFromFile();
    }
    return OnceDate;
}

char const *GetEnglishErr(int r)
{
    if (r < 0 || r >= NumErrs) {
        r = E_SWERR;
    }
    return ErrMsg[r];
}

char const *GetErr(int r)
{
    char const *msg;

    if (r < 0 || r >= NumErrs) {
        r = E_SWERR;
    }

    msg = GetTranslatedString(ErrMsg[r]);
    if (!msg) {
        return ErrMsg[r];
    }

    return msg;
}
