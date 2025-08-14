/***************************************************************/
/*                                                             */
/*  INIT.C                                                     */
/*                                                             */
/*  Initialize remind; perform certain tasks between           */
/*  iterations in calendar mode; do certain checks after end   */
/*  in normal mode.                                            */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "version.h"
#include "config.h"

#define L_IN_INIT 1
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#ifdef HAVE_INITGROUPS
#include <grp.h>
#endif

#include "types.h"
#include "globals.h"
#include "protos.h"
#include "err.h"

static int should_guess_terminal_background = 1;

static void guess_terminal_background(int *r, int *g, int *b);
static int tty_init(int fd);
static void tty_raw(int fd);
static void tty_reset(int fd);

static void ProcessLongOption(char const *arg);
/***************************************************************
 *
 *  Command line options recognized:
 *
 *  -n       = Output next trigger date of each reminder in
 *             simple calendar format.
 *  -r       = Disallow RUN mode
 *  -c[n]    = Produce a calendar for n months (default = 1)
 *  -@[n,m,b]= Colorize n=0 VT100 n=1 85 n=2 True m=0 dark terminal m=1 light
 *             b=0 ignore SHADE b=1 respect SHADE
 *  -w[n,n,n] = Specify output device width, padding and spacing
 *  -s[n]    = Produce calendar in "simple calendar" format
 *  -p[n]    = Produce calendar in format compatible with rem2ps
 *  -l       = Prefix simple calendar lines with a comment containing
 *             their trigger line numbers and filenames
 *  -v       = Verbose mode
 *  -o       = Ignore ONCE directives
 *  -a       = Don't issue timed reminders which will be queued
 *  -q       = Don't queue timed reminders
 *  -t       = Trigger all reminders (infinite delta)
 *  -h       = Hush mode
 *  -f       = Do not fork
 *  -dchars  = Debugging mode:  Chars are:
 *             f = Trace file openings
 *             e = Echo input lines
 *             x = Display expression evaluation
 *             t = Display trigger dates
 *             v = Dump variables at end
 *             l = Display entire line in error messages
 *             s = Display expression-parsing stack usage before exit
 *  -e       = Send messages normally sent to stderr to stdout instead
 *  -z[n]    = Daemon mode waking up every n (def 1) minutes.
 *  -bn      = Time format for cal (0, 1, or 2)
 *  -xn      = Max. number of iterations for SATISFY
 *  -uname   = Run as user 'name' - only valid when run by root.  If run
 *             by non-root, changes environment but not effective uid.
 *  -kcmd    = Run 'cmd' for MSG-type reminders instead of printing to stdout
 *  -iVAR=EXPR = Initialize and preserve VAR.
 *  -m       = Start calendar with Monday instead of Sunday.
 *  -j[n]    = Purge all junk from reminder files (n = INCLUDE depth)
 *  A minus sign alone indicates to take input from stdin
 *
 **************************************************************/

/* For parsing an integer */
#define PARSENUM(var, s)   \
var = 0;                   \
while (isdigit(*(s))) {    \
    var *= 10;             \
    var += *(s) - '0';     \
    s++;                   \
}

static void ChgUser(char const *u);
static void InitializeVar(char const *str);

static char const *BadDate = "Illegal date on command line\n";
static void AddTrustedUser(char const *username);

static DynamicBuffer default_filename_buf;

static void
InitCalWidthAndFormWidth(int fd)
{
    struct winsize w;

    if (!isatty(fd)) {
        return;
    }
    if (ioctl(fd, TIOCGWINSZ, &w) == 0) {
        CalWidth = w.ws_col;
        if (CalWidth < 71) {
            CalWidth = 71;
        }
        FormWidth = w.ws_col - 8;
        if (FormWidth < 20) FormWidth = 20;
        if (FormWidth > 500) FormWidth = 500;
    }
}

/***************************************************************/
/*                                                             */
/*  DefaultFilename                                            */
/*                                                             */
/*  If we're invoked as "rem" rather than "remind", use a      */
/*  default filename.  Use $DOTREMINDERS or $HOME/.reminders   */
/*                                                             */
/***************************************************************/
static char const *DefaultFilename(void)
{
    char const *s;

    DBufInit(&default_filename_buf);

    s = getenv("DOTREMINDERS");
    if (s) {
        return s;
    }

    s = getenv("HOME");
    if (!s) {
        fprintf(ErrFp, "HOME environment variable not set.  Unable to determine reminder file.\n");
        exit(EXIT_FAILURE);
    }
    DBufPuts(&default_filename_buf, s);
    DBufPuts(&default_filename_buf, "/.reminders");
    return DBufValue(&default_filename_buf);
}

/***************************************************************/
/*                                                             */
/*  InitRemind                                                 */
/*                                                             */
/*  Initialize the system - called only once at beginning!     */
/*                                                             */
/***************************************************************/
void InitRemind(int argc, char const *argv[])
{
    char const *arg;
    int i;
    int y, m, d, rep;
    Token tok;
    int InvokedAsRem = 0;
    char const *s;
    int weeks;
    int x;
    int dse;
    int ttyfd;

    dse = NO_DATE;

    /* Initialize variable hash table */
    InitVars();

    /* Initialize user-defined functions hash table */
    InitUserFunctions();

    InitTranslationTable();
    InitFiles();

    /* If stdout is a terminal, initialize $FormWidth to terminal width-8,
       but clamp to [20, 500] */
    InitCalWidthAndFormWidth(STDOUT_FILENO);

    /* Initialize global dynamic buffers */
    DBufInit(&Banner);
    DBufInit(&LineBuffer);
    DBufInit(&ExprBuf);

    DBufPuts(&Banner, "Reminders for %w, %d%s %m, %y%o:");

    PurgeFP = NULL;

    InitDedupeTable();

    /* Make sure remind is not installed set-uid or set-gid */
    if (getgid() != getegid() ||
        getuid() != geteuid()) {
        fprintf(ErrFp, "\nRemind should not be installed set-uid or set-gid.\nCHECK YOUR SYSTEM SECURITY.\n");
        exit(EXIT_FAILURE);
    }

    y = NO_YR;
    m = NO_MON;
    d = NO_DAY;
    rep = NO_REP;

    RealToday = SystemDate(&CurYear, &CurMon, &CurDay);
    if (RealToday < 0) {
        fprintf(ErrFp, GetErr(M_BAD_SYS_DATE), BASE);
        fprintf(ErrFp, "\n");
        exit(EXIT_FAILURE);
    }
    DSEToday = RealToday;
    FromDSE(DSEToday, &CurYear, &CurMon, &CurDay);

    /* Initialize Latitude and Longitude */
    set_components_from_lat_and_long();

    /* See if we were invoked as "rem" rather than "remind" */
    if (argv[0]) {
        s = strrchr(argv[0], '/');
        if (!s) {
            s = argv[0];
        } else {
            s++;
        }
        if (!strcmp(s, "rem")) {
            InvokedAsRem = 1;
        }
    } else {
        fprintf(ErrFp, "Invoked with a NULL argv[0]; bailing because that's just plain bizarre.\n");
        exit(EXIT_FAILURE);
    }

    /* Parse the command-line options */
    i = 1;

    while (i < argc) {
        arg = argv[i];
        if (*arg != '-') break; /* Exit the loop if it's not an option */
        i++;
        arg++;
        if (!*arg) {
            UseStdin = 1;
            i--;
            break;
        }
        while (*arg) {
            switch(*arg++) {
            case '+':
                AddTrustedUser(arg);
                while(*arg) arg++;
                break;

            case '-':
                ProcessLongOption(arg);
                while(*arg) arg++;
                break;

            case '@':
                UseVTColors = 1;
                if (*arg) {
                    PARSENUM(x, arg);
                    if (x == 1) {
                        Use256Colors = 1;
                    } else if (x == 2) {
                        UseTrueColors = 1;
                    } else if (x != 0) {
                        fprintf(ErrFp, "%s: -@n,m,b: n must be 0, 1 or 2 (assuming 0)\n",
                                argv[0]);
                    }
                }
                if (*arg == ',') {
                    arg++;
                    if (*arg != ',') {
                        if (*arg == 't') {
                            arg++;
                            should_guess_terminal_background = 2;
                        } else {
                            PARSENUM(x, arg);
                            if (x == 0) {
                                should_guess_terminal_background = 0;
                                TerminalBackground = TERMINAL_BACKGROUND_DARK;
                            } else if (x == 1) {
                                should_guess_terminal_background = 0;
                                TerminalBackground = TERMINAL_BACKGROUND_LIGHT;
                            } else if (x == 2) {
                                should_guess_terminal_background = 0;
                                TerminalBackground = TERMINAL_BACKGROUND_UNKNOWN;
                            } else {
                                fprintf(ErrFp, "%s: -@n,m,b: m must be t, 0, 1 or 2 (assuming 2)\n",
                                        argv[0]);
                            }
                        }
                    }
                }
                if (*arg == ',') {
                    arg++;
                    PARSENUM(x, arg);
                    if (x != 0 && x != 1) {
                        fprintf(ErrFp, "%s: -@n,m,b: b must be 0 or 1 (assuming 0)\n",
                                argv[0]);
                        x = 0;
                    }
                    UseBGVTColors = x;
                }
                break;

            case 'j':
            case 'J':
                PurgeMode = 1;
                if (*arg) {
                    PARSENUM(PurgeIncludeDepth, arg);
                }
                break;
            case 'i':
            case 'I':
                InitializeVar(arg);
                while(*arg) arg++;
                break;

            case 'n':
            case 'N':
                NextMode = 1;
                DontQueue = 1;
                Daemon = 0;
                IgnoreOnce = 1;
                break;

            case 'r':
            case 'R':
                RunDisabled = RUN_CMDLINE;
                break;

            case 'm':
            case 'M':
                MondayFirst = 1;
                break;

            case 'o':
            case 'O':
                IgnoreOnce = 1;
                break;

            case 'y':
            case 'Y':
                SynthesizeTags = 1;
                break;

            case 't':
            case 'T':
                if (*arg == 'T' || *arg == 't') {
                    arg++;
                    if (!*arg) {
                        DefaultTDelta = 5;
                    } else {
                        PARSENUM(DefaultTDelta, arg);
                        if (DefaultTDelta < 0) {
                            DefaultTDelta = 0;
                        } else if (DefaultTDelta > MINUTES_PER_DAY) {
                            DefaultTDelta = MINUTES_PER_DAY;
                        }
                    }
                } else if (!*arg) {
                    InfiniteDelta = 1;
                } else {
                    if (*arg == 'z') {
                        DeltaOverride = -1;
                        arg++;
                    } else {
                        PARSENUM(DeltaOverride, arg);
                        if (DeltaOverride < 0) {
                            DeltaOverride = 0;
                        }
                    }
                }
                break;
            case 'e':
            case 'E':
                ErrFp = stdout;
                break;

            case 'h':
            case 'H':
                Hush = 1;
                break;

            case 'g':
            case 'G':
                SortByDate = SORT_ASCEND;
                SortByTime = SORT_ASCEND;
                SortByPrio = SORT_ASCEND;
                UntimedBeforeTimed = 0;
                if (*arg) {
                    if (*arg == 'D' || *arg == 'd')
                        SortByDate = SORT_DESCEND;
                    arg++;
                }
                if (*arg) {
                    if (*arg == 'D' || *arg == 'd')
                        SortByTime = SORT_DESCEND;
                    arg++;
                }
                if (*arg) {
                    if (*arg == 'D' || *arg == 'd')
                        SortByPrio = SORT_DESCEND;
                    arg++;
                }
                if (*arg) {
                    if (*arg == 'D' || *arg == 'd')
                        UntimedBeforeTimed = 1;
                    arg++;
                }
                break;

            case 'u':
            case 'U':
                if (*arg == '+') {
                    ChgUser(arg+1);
                } else {
                    RunDisabled = RUN_CMDLINE;
                    ChgUser(arg);
                }
                while (*arg) arg++;
                break;
            case 'z':
            case 'Z':
                DontFork = 1;
                if (*arg == 'j' || *arg == 'J') {
                    while (*arg) arg++;
                    Daemon = -1;
                    DaemonJSON = 1;
                } else if (*arg == '0') {
                    PARSENUM(Daemon, arg);
                    if (Daemon == 0) Daemon = -1;
                    else if (Daemon < 1) Daemon = 1;
                    else if (Daemon > 60) Daemon = 60;
                } else {
                    PARSENUM(Daemon, arg);
                    if (Daemon<1) Daemon=1;
                    else if (Daemon>60) Daemon=60;
                }
                break;

            case 'a':
            case 'A':
                DontIssueAts++;
                break;

            case 'q':
            case 'Q':
                DontQueue = 1;
                break;

            case 'f':
            case 'F':
                DontFork = 1;
                break;
            case 'c':
            case 'C':
                IgnoreOnce = 1;
                DoCalendar = 1;
                weeks = 0;
                /* Parse the flags */
                while(*arg) {
                    if (*arg == 'a' ||
                        *arg == 'A') {
                        DoSimpleCalDelta = 1;
                        arg++;
                        continue;
                    }
                    if (*arg == '+') {
                        weeks = 1;
                        arg++;
                        continue;
                    }
                    if (*arg == 'l' || *arg == 'L') {
                        UseVTChars = 1;
                        arg++;
                        continue;
                    }
                    if (*arg == 'u' || *arg == 'U') {
                        UseUTF8Chars = 1;
                        arg++;
                        continue;
                    }
                    if (*arg == 'c' || *arg == 'C') {
                        UseVTColors = 1;
                        arg++;
                        continue;
                    }
                    break;
                }
                if (weeks) {
                    CalType = "weekly";
                    PARSENUM(CalWeeks, arg);
                    if (!CalWeeks) CalWeeks = 1;
                } else {
                    CalType = "monthly";
                    PARSENUM(CalMonths, arg);
                    if (!CalMonths) CalMonths = 1;
                }
                break;

            case 's':
            case 'S':
                DoSimpleCalendar = 1;
                IgnoreOnce = 1;
                weeks = 0;
                while(*arg) {
                    if (*arg == 'a' || *arg == 'A') {
                        DoSimpleCalDelta = 1;
                        arg++;
                        continue;
                    }
                    if (*arg == '+') {
                        arg++;
                        weeks = 1;
                        continue;
                    }
                    break;
                }
                if (weeks) {
                    CalType = "weekly";
                    PARSENUM(CalWeeks, arg);
                    if (!CalWeeks) CalWeeks = 1;
                } else {
                    CalType = "monthly";
                    PARSENUM(CalMonths, arg);
                    if (!CalMonths) CalMonths = 1;
                }
                break;

            case 'p':
            case 'P':
                DoSimpleCalendar = 1;
                IgnoreOnce = 1;
                PsCal = PSCAL_LEVEL1;
                weeks = 0;
                while (*arg == 'a' || *arg == 'A' ||
                       *arg == 'q' || *arg == 'Q' ||
                       *arg == '+' ||
                       *arg == 'p' || *arg == 'P') {
                    if (*arg == '+') {
                        weeks = 1;
                    } else if (*arg == 'a' || *arg == 'A') {
                        DoSimpleCalDelta = 1;
                    } else if (*arg == 'p' || *arg == 'P') {
                        /* JSON interchange formats always include
                           file and line number info */
                        DoPrefixLineNo = 1;
                        if (PsCal == PSCAL_LEVEL1) {
                            PsCal = PSCAL_LEVEL2;
                        } else {
                            PsCal = PSCAL_LEVEL3;
                        }
                    } else if (*arg == 'q' || *arg == 'Q') {
                        DontSuppressQuoteMarkers = 1;
                    }
                    arg++;
                }
                if (weeks) {
                    CalType = "weekly";
                    PARSENUM(CalWeeks, arg);
                    if (!CalWeeks) CalWeeks = 1;
                    PsCal = PSCAL_LEVEL3;
                } else {
                    CalType = "monthly";
                    PARSENUM(CalMonths, arg);
                    if (!CalMonths) CalMonths = 1;
                }
                break;

            case 'l':
            case 'L':
                DoPrefixLineNo = 1;
                break;

            case 'w':
            case 'W':
                if (*arg != ',') {
                    if (*arg == 't') {
                        arg++;
                        /* -wt means get width from /dev/tty */
                        ttyfd = open("/dev/tty", O_RDONLY);
                        if (ttyfd < 0) {
                            fprintf(ErrFp, "%s: `-wt': Cannot open /dev/tty: %s\n",
                                    argv[0], strerror(errno));
                        } else {
                            InitCalWidthAndFormWidth(ttyfd);
                            close(ttyfd);
                        }
                    } else {
                        PARSENUM(CalWidth, arg);
                        if (CalWidth != 0 && CalWidth < 71) CalWidth = 71;
                        if (CalWidth == 0) {
                            /* Cal width of 0 means obtain from stdout */
                            if (isatty(STDOUT_FILENO)) {
                                InitCalWidthAndFormWidth(STDOUT_FILENO);
                            } else {
                                CalWidth = 80;
                            }
                        }
                        FormWidth = CalWidth - 8;
                        if (FormWidth < 20) FormWidth = 20;
                        if (FormWidth > 500) FormWidth = 500;
                    }
                }
                if (*arg == ',') {
                    arg++;
                    if (*arg != ',') {
                        PARSENUM(CalLines, arg);
                        if (CalLines > 20) CalLines = 20;
                    }
                    if (*arg == ',') {
                        arg++;
                        PARSENUM(CalPad, arg);
                        if (CalPad > 20) CalPad = 20;
                    }
                }
                break;

            case 'd':
            case 'D':
                while (*arg) {
                    switch(*arg++) {
                    case 's': case 'S': DebugFlag |= DB_PARSE_EXPR;  break;
                    case 'h': case 'H': DebugFlag |= DB_HASHSTATS;   break;
                    case 'e': case 'E': DebugFlag |= DB_ECHO_LINE;   break;
                    case 'x': case 'X': DebugFlag |= DB_PRTEXPR;     break;
                    case 't': case 'T': DebugFlag |= DB_PRTTRIG;     break;
                    case 'v': case 'V': DebugFlag |= DB_DUMP_VARS;   break;
                    case 'l': case 'L': DebugFlag |= DB_PRTLINE;     break;
                    case 'f': case 'F': DebugFlag |= DB_TRACE_FILES; break;
                    case 'q': case 'Q': DebugFlag |= DB_TRANSLATE;   break;
                    case 'n': case 'N': DebugFlag |= DB_NONCONST;    break;
                    case 'u': case 'U': DebugFlag |= DB_UNUSED_VARS; break;
                    default:
                        fprintf(ErrFp, GetErr(M_BAD_DB_FLAG), *(arg-1));
                        fprintf(ErrFp, "\n");
                    }
                }
                break;

            case 'v':
            case 'V':
                DebugFlag |= DB_PRTLINE;
                ShowAllErrors = 1;
                break;

            case 'b':
            case 'B':
                PARSENUM(ScFormat, arg);
                if (ScFormat<0 || ScFormat>2) ScFormat=SC_AMPM;
                break;

            case 'x':
            case 'X':
                PARSENUM(MaxSatIter, arg);
                if (MaxSatIter < 10) MaxSatIter=10;
                break;

            case 'k':
            case 'K':
                if (*arg == ':') {
                    arg++;
                    QueuedMsgCommand = arg;
                } else {
                    MsgCommand = arg;
                }
                while (*arg) arg++;  /* Chew up remaining chars in this arg */
                break;

            default:
                fprintf(ErrFp, GetErr(M_BAD_OPTION), *(arg-1));
                fprintf(ErrFp, "\n");
            }

        }
    }

    /* Get the filename. */
    if (!InvokedAsRem) {
        if (i >= argc) {
            Usage();
            exit(EXIT_FAILURE);
        }
        InitialFile = argv[i++];
    } else {
        InitialFile = DefaultFilename();
    }

    /* Get the date, if any */
    if (i < argc) {
        while (i < argc) {
            arg = argv[i++];
            FindToken(arg, &tok);
            switch (tok.type) {
            case T_Time:
                if (SysTime != -1L) Usage();
                else {
                    SysTime = (long) tok.val * 60L;
                    DontQueue = 1;
                    Daemon = 0;
                }
                break;

            case T_DateTime:
                if (SysTime != -1L) Usage();
                if (m != NO_MON || d != NO_DAY || y != NO_YR || dse != NO_DATE) Usage();
                SysTime = (tok.val % MINUTES_PER_DAY) * 60;
                DontQueue = 1;
                Daemon = 0;
                dse = tok.val / MINUTES_PER_DAY;
                break;

            case T_Date:
                if (m != NO_MON || d != NO_DAY || y != NO_YR || dse != NO_DATE) Usage();
                dse = tok.val;
                break;

            case T_Month:
                if (m != NO_MON || dse != NO_DATE) Usage();
                else m = tok.val;
                break;

            case T_Day:
                if (d != NO_DAY || dse != NO_DATE) Usage();
                else d = tok.val;
                break;

            case T_Year:
                if (y != NO_YR || dse != NO_DATE) Usage();
                else y = tok.val;
                break;

            case T_Rep:
                if (rep != NO_REP) Usage();
                else rep = tok.val;
                break;

            default:
                if (tok.type == T_Illegal && tok.val < 0) {
                    fprintf(ErrFp, "%s: `%s'\n", GetErr(-tok.val), arg);
                    Usage();
                }
                Usage();
            }
        }

        if (rep > 0) {
            Iterations = rep;
            IgnoreOnce = 1;
            DontQueue = 1;
            Daemon = 0;
        }

        if (dse != NO_DATE) {
            FromDSE(dse, &y, &m, &d);
        }
/* Must supply date in the form:  day, mon, yr OR mon, yr */
        if (m != NO_MON || y != NO_YR || d != NO_DAY) {
            if (m == NO_MON || y == NO_YR) {
                if (rep == NO_REP) Usage();
                else if (m != NO_MON || y != NO_YR) Usage();
                else {
                    m = CurMon;
                    y = CurYear;
                    if (d == NO_DAY) d = CurDay;
                }
            }
            if (d == NO_DAY) d=1;
            if (d > DaysInMonth(m, y)) {
                fprintf(ErrFp, "%s", BadDate);
                Usage();
            }
            DSEToday = DSE(y, m, d);
            if (DSEToday == -1) {
                fprintf(ErrFp, "%s", BadDate);
                Usage();
            }
            CurYear = y;
            CurMon = m;
            CurDay = d;
            if (DSEToday != RealToday) IgnoreOnce = 1;
        }

    }

    /* JSON mode turns off sorting */
    if (JSONMode) {
        SortByTime = SORT_NONE;
        SortByDate = SORT_NONE;
        SortByPrio = SORT_NONE;
    }

    /* Figure out the offset from UTC */
    if (CalculateUTC) {
        (void) CalcMinsFromUTC(DSEToday, MinutesPastMidnight(0),
                               &MinsFromUTC, NULL);
    }
}

/***************************************************************/
/*                                                             */
/*  Usage                                                      */
/*                                                             */
/*  Print the usage info.                                      */
/*                                                             */
/***************************************************************/
#ifndef L_USAGE_OVERRIDE
void Usage(void)
{
    fprintf(ErrFp, "\nREMIND %s Copyright (C) 1992-2025 Dianne Skoll\n", VERSION);
#ifdef BETA
    fprintf(ErrFp, ">>>> BETA VERSION <<<<\n");
#endif
    fprintf(ErrFp, "Usage: remind [options] filename [date] [time] [*rep]\n");
    fprintf(ErrFp, "Options:\n");
    fprintf(ErrFp, " -n     Output next occurrence of reminders in simple format\n");
    fprintf(ErrFp, " -r     Disable RUN directives\n");
    fprintf(ErrFp, " -@[n,m,b] Colorize COLOR/SHADE reminders\n");
    fprintf(ErrFp, " -c[a][n] Produce a calendar for n (default 1) months\n");
    fprintf(ErrFp, " -c[a]+[n] Produce a calendar for n (default 1) weeks\n");
    fprintf(ErrFp, " -w[n[,p[,s]]]  Specify width, padding and spacing of calendar\n");
    fprintf(ErrFp, " -s[a][+][n] Produce `simple calendar' for n (1) months (weeks)\n");
    fprintf(ErrFp, " -p[a][n] Same as -s, but input compatible with rem2ps\n");
    fprintf(ErrFp, " -l     Prefix each simple calendar line with line number and filename comment\n");
    fprintf(ErrFp, " -v     Verbose mode\n");
    fprintf(ErrFp, " -o     Ignore ONCE directives\n");
    fprintf(ErrFp, " -t[n]  Trigger all future (or those within `n' days)\n");
    fprintf(ErrFp, " -h     `Hush' mode - be very quiet\n");
    fprintf(ErrFp, " -a     Don't trigger timed reminders immediately - just queue them\n");
    fprintf(ErrFp, " -q     Don't queue timed reminders\n");
    fprintf(ErrFp, " -f     Trigger timed reminders by staying in foreground\n");
    fprintf(ErrFp, " -z[n]  Enter daemon mode, waking every n (1) minutes.\n");
    fprintf(ErrFp, " -d...  Debug: See man page for details\n");
    fprintf(ErrFp, " -e     Divert messages normally sent to stderr to stdout\n");
    fprintf(ErrFp, " -b[n]  Time format for cal: 0=am/pm, 1=24hr, 2=none\n");
    fprintf(ErrFp, " -x[n]  Iteration limit for SATISFY clause (def=1000)\n");
    fprintf(ErrFp, " -kcmd  Run `cmd' for MSG-type reminders\n");
    fprintf(ErrFp, " -g[dddd] Sort reminders by date, time, priority, and 'timedness'\n");
    fprintf(ErrFp, " -ivar=val Initialize var to val and preserve var\n");
    fprintf(ErrFp, " -m     Start calendar with Monday rather than Sunday\n");
    fprintf(ErrFp, " -y     Synthesize tags for tagless reminders\n");
    fprintf(ErrFp, " -j[n]  Run in 'purge' mode.  [n = INCLUDE depth]\n");
    fprintf(ErrFp, "\nLong Options:\n");
    fprintf(ErrFp, " --version                Print Remind version\n");
    fprintf(ErrFp, " --hide-completed-todos   Don't show completed todos on calendar\n");
    fprintf(ErrFp, " --only-todos             Only issue TODO reminders\n");
    fprintf(ErrFp, " --only-events            Do not issue TODO reminders\n");
    fprintf(ErrFp, " --json                   Use JSON output instead of plain-text\n");
    fprintf(ErrFp, " --max-execution-time=n   Limit execution time to n seconds\n");
    fprintf(ErrFp, " --print-config-cmd       Print ./configure cmd used to build Remind\n");
    fprintf(ErrFp, " --print-errs             Print all possible error messages\n");
    fprintf(ErrFp, " --print-tokens           Print all possible Remind tokens\n");
    fprintf(ErrFp, "\nRemind home page: %s\n", PACKAGE_URL);
    exit(EXIT_FAILURE);
}
#endif /* L_USAGE_OVERRIDE */
/***************************************************************/
/*                                                             */
/*  ChgUser                                                    */
/*                                                             */
/*  Run as a specified user.  Can only be used if Remind is    */
/*  started by root.  This changes the real and effective uid, */
/*  the real and effective gid, and sets the HOME, SHELL and   */
/*  USER environment variables.                                */
/*                                                             */
/***************************************************************/
static void ChgUser(char const *user)
{
    uid_t myeuid;

    struct passwd *pwent;
    static char *home;
    static char *shell;
    static char *username;
    static char *logname;

    myeuid = geteuid();

    pwent = getpwnam(user);

    if (!pwent) {
        fprintf(ErrFp, GetErr(M_BAD_USER), user);
        fprintf(ErrFp, "\n");
        exit(EXIT_FAILURE);
    }

    if (!myeuid) {
        /* Started as root, so drop privileges */
#ifdef HAVE_INITGROUPS
        if (initgroups(pwent->pw_name, pwent->pw_gid) < 0) {
            fprintf(ErrFp, GetErr(M_NO_CHG_GID), pwent->pw_gid);
            fprintf(ErrFp, "\n");
            exit(EXIT_FAILURE);
        };
#endif
        if (setgid(pwent->pw_gid) < 0) {
            fprintf(ErrFp, GetErr(M_NO_CHG_GID), pwent->pw_gid);
            fprintf(ErrFp, "\n");
            exit(EXIT_FAILURE);
        }

        if (setuid(pwent->pw_uid) < 0) {
            fprintf(ErrFp, GetErr(M_NO_CHG_UID), pwent->pw_uid);
            fprintf(ErrFp, "\n");
            exit(EXIT_FAILURE);
        }
    }

    home = malloc(strlen(pwent->pw_dir) + 6);
    if (!home) {
        fprintf(ErrFp, "%s", GetErr(M_NOMEM_ENV));
        fprintf(ErrFp, "\n");
        exit(EXIT_FAILURE);
    }
    sprintf(home, "HOME=%s", pwent->pw_dir);
    putenv(home);

    shell = malloc(strlen(pwent->pw_shell) + 7);
    if (!shell) {
        fprintf(ErrFp, "%s", GetErr(M_NOMEM_ENV));
        fprintf(ErrFp, "\n");
        exit(EXIT_FAILURE);
    }
    sprintf(shell, "SHELL=%s", pwent->pw_shell);
    putenv(shell);

    if (pwent->pw_uid) {
        username = malloc(strlen(pwent->pw_name) + 6);
        if (!username) {
            fprintf(ErrFp, "%s", GetErr(M_NOMEM_ENV));
            fprintf(ErrFp, "\n");
            exit(EXIT_FAILURE);
        }
        sprintf(username, "USER=%s", pwent->pw_name);
        putenv(username);
        logname= malloc(strlen(pwent->pw_name) + 9);
        if (!logname) {
            fprintf(ErrFp, "%s", GetErr(M_NOMEM_ENV));
            fprintf(ErrFp, "\n");
            exit(EXIT_FAILURE);
        }
        sprintf(logname, "LOGNAME=%s", pwent->pw_name);
        putenv(logname);
    }
}

static void
DefineFunction(char const *str)
{
    Parser p;
    int r;

    CreateParser(str, &p);
    r = DoFset(&p);
    DestroyParser(&p);
    if (r != OK) {
        fprintf(ErrFp, "-i option: %s: %s\n", str, GetErr(r));
    }
}
/***************************************************************/
/*                                                             */
/*  InitializeVar                                              */
/*                                                             */
/*  Initialize and preserve a variable                         */
/*                                                             */
/***************************************************************/
static void InitializeVar(char const *str)
{
    char const *expr;
    char const *ostr = str;
    char varname[VAR_NAME_LEN+1];

    Value val;

    int r;

    /* Scan for an '=' sign */
    r = 0;
    while (*str && *str != '=') {
        if (r < VAR_NAME_LEN) {
            if (isalpha(*str) || *str == '_' || (r > 0 && *str == '(') || (r == 0 && *str == '$') || (r > 0 && isdigit(*str))) {
                varname[r++] = *str;
            } else {
                fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(E_ILLEGAL_CHAR));
                fprintf(ErrFp, "\n");
                return;
            }
        }
        if (*str == '(') {
            /* Do a function definition if we see a paren */
            DefineFunction(ostr);
            return;
        }
        str++;
    }
    varname[r] = 0;
    if (!*varname) {
        fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(E_MISS_VAR));
        fprintf(ErrFp, "\n");
        return;
    }
    if (!*str) {
        /* Setting a system var does require =expr on the commandline */
        if (*varname == '$') {
            fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(E_MISS_EQ));
            fprintf(ErrFp, "\n");
            return;
        }
        val.type = INT_TYPE;
        val.v.val = 0;
        r = SetVar(varname, &val, 1);
        if (!r) {
            r = PreserveVar(varname);
        }
        if (r) {
            fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(r));
            fprintf(ErrFp, "\n");
        }
        return;
    }

    expr = str+1;
    if (!*expr) {
        fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(E_MISS_EXPR));
        fprintf(ErrFp, "\n");
        return;
    }

    r=EvalExpr(&expr, &val, NULL);
    if (r) {
        fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(r));
        fprintf(ErrFp, "\n");
        return;
    }

    if (*varname == '$') {
        r=SetSysVar(varname+1, &val);
        DestroyValue(val);
        if (r) {
            fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(r));
            fprintf(ErrFp, "\n");
        }
        return;
    }

    r=SetVar(varname, &val, 1);
    if (r) {
        fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(r));
        fprintf(ErrFp, "\n");
        return;
    }
    r=PreserveVar(varname);
    if (r) {
        fprintf(ErrFp, GetErr(M_I_OPTION), GetErr(r));
        fprintf(ErrFp, "\n");
    }
    return;
}

static void
AddTrustedUser(char const *username)
{
    struct passwd const *pwent;
    if (NumTrustedUsers >= MAX_TRUSTED_USERS) {
        fprintf(ErrFp, "Too many trusted users (%d max)\n",
                MAX_TRUSTED_USERS);
        exit(EXIT_FAILURE);
    }

    pwent = getpwnam(username);
    if (!pwent) {
        fprintf(ErrFp, GetErr(M_BAD_USER), username);
        fprintf(ErrFp, "\n");
        exit(EXIT_FAILURE);
    }
    TrustedUsers[NumTrustedUsers] = pwent->pw_uid;
    NumTrustedUsers++;
}

static pid_t LimiterPid = (pid_t) -1;

void unlimit_execution_time(void)
{
    if (LimiterPid != (pid_t) -1) {
        kill(LimiterPid, SIGTERM);
        LimiterPid = (pid_t) -1;
    }
}

static void limit_execution_time(int t)
{
    pid_t parent = getpid();

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }

    if (pid > 0) {
        LimiterPid = pid;
        /* In the parent */
        return;
    }

    /* In the child */
    time_t start = time(NULL);
    while(1) {
        sleep(1);
        if (kill(parent, 0) < 0) {
            /* Parent has probably exited */
            exit(0);
        }
        if (time(NULL) - start > t) {
            kill(parent, SIGXCPU);
            exit(0);
        }
    }
}

static void
ProcessLongOption(char const *arg)
{
    int t;
    if (!strcmp(arg, "test")) {
        fprintf(stderr, "Enabling test mode: This is meant for the acceptance test.\nDo not use --test in production.\nIn test mode, the system time is fixed at 2025-01-06@19:00\n");
        TestMode = 1;

        /* Update RealToday because of TestMode */
        RealToday = SystemDate(&CurYear, &CurMon, &CurDay);
        DSEToday = RealToday;
        FromDSE(DSEToday, &CurYear, &CurMon, &CurDay);

        return;
    }
    if (!strcmp(arg, "only-todos")) {
        if (TodoFilter == ONLY_EVENTS) {
            fprintf(ErrFp, "remind: Cannot combine --only-todos and --only-events\n");
            exit(1);
        }
        TodoFilter = ONLY_TODOS;
        return;
    }
    if (!strcmp(arg, "only-events")) {
        if (TodoFilter == ONLY_TODOS) {
            fprintf(ErrFp, "remind: Cannot combine --only-todos and --only-events\n");
            exit(1);
        }
        TodoFilter = ONLY_EVENTS;
        return;
    }
    if (!strcmp(arg, "json")) {
        JSONMode = 1;
        DontQueue = 1;
        return;
    }

    if (!strcmp(arg, "version")) {
        printf("%s\n", VERSION);
        exit(EXIT_SUCCESS);
    }
    if (!strcmp(arg, "print-config-cmd")) {
        printf("%s\n", CONFIG_CMD);
        exit(EXIT_SUCCESS);
    }
    if (!strcmp(arg, "print-errs")) {
        for (t=0; t<NumErrs; t++) {
            if (*ErrMsg[t]) {
                print_escaped_string(stdout, ErrMsg[t]);
                printf("\n");
            }
        }
        exit(EXIT_SUCCESS);
    }

    if (!strcmp(arg, "hide-completed-todos")) {
        HideCompletedTodos = 1;
        return;
    }

    if (!strcmp(arg, "print-tokens")) {
        print_remind_tokens();
        print_builtinfunc_tokens();
        print_sysvar_tokens();
        exit(0);
    }
    if (sscanf(arg, "max-execution-time=%d", &t) == 1) {
        if (t < 0) {
            fprintf(ErrFp, "%s: --max-execution-time must be non-negative\n", ArgV[0]);
            return;
        }
        if (t > 0) {
            limit_execution_time(t);
        }
        return;
    }
    fprintf(ErrFp, "%s: Unknown long option --%s\n", ArgV[0], arg);
}

static void
guess_terminal_background(int *r, int *g, int *b)
{
    int ttyfd;
    struct pollfd p;
    unsigned int rr, gg, bb;
    char buf[128];
    int n;

    *r = -1;
    *g = -1;
    *b = -1;

    /* Don't guess if stdout not a terminal unless asked to by @,t */
    if (should_guess_terminal_background != 2) {
        if (!isatty(STDOUT_FILENO)) {
            return;
        }
    }

    ttyfd = open("/dev/tty", O_RDWR);
    if (ttyfd < 0) {
        return;
    }

    if (!isatty(ttyfd)) {
        /* Not a TTY: Can't guess the color */
        close(ttyfd);
        return;
    }

    if (!tty_init(ttyfd)) {
        return;
    }
    tty_raw(ttyfd);
    n = write(ttyfd, "\033]11;?\033\\", 8);

    if (n != 8) {
        /* write failed... WTF?  Not much we can do */
        tty_reset(ttyfd);
        close(ttyfd);
        return;
    }

    /* Wait up to 0.1s for terminal to respond */
    p.fd = ttyfd;
    p.events = POLLIN;
    if (poll(&p, 1, 100) < 0) {
        tty_reset(ttyfd);
        close(ttyfd);
        return;
    }
    if (!(p.revents & POLLIN)) {
        tty_reset(ttyfd);
        close(ttyfd);
        return;
    }
    n = read(ttyfd, buf, 127);
    if (n <= 0) {
        tty_reset(ttyfd);
        close(ttyfd);
        return;
    }
    tty_reset(ttyfd);
    close(ttyfd);
    buf[n+1] = 0;
    if (n < 25) {
        /* Too short */
        return;
    }
    if (sscanf(buf+5, "rgb:%x/%x/%x", &rr, &gg, &bb) != 3) {
        /* Couldn't scan color codes */
        return;
    }
    *r = (int) ((rr >> 8) & 255);
    *g = (int) ((gg >> 8) & 255);
    *b = (int) ((bb >> 8) & 255);
}

static struct termios orig_termios;

static int
tty_init(int fd)
{
    if (tcgetattr(fd, &orig_termios) < 0) {
        return 0;
    }
    return 1;
}

static void
tty_raw(int fd)
{
    struct termios raw;

    raw = orig_termios;

    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* put terminal in raw mode after flushing */
    tcsetattr(fd,TCSAFLUSH,&raw);
}

static void
tty_reset(int fd)
{
    tcsetattr(fd, TCSAFLUSH, &orig_termios);
}

int
GetTerminalBackground(void)
{
    int r, g, b;
    if (should_guess_terminal_background) {
        guess_terminal_background(&r, &g, &b);
        if (r >= 0 && g >= 0 && b >= 0) {
            if (r+g+b <= 85*3 && r <= 128 && g <= 128 && b <= 128) {
                TerminalBackground = TERMINAL_BACKGROUND_DARK;
            } else {
                TerminalBackground = TERMINAL_BACKGROUND_LIGHT;
            }
        }
        should_guess_terminal_background = 0;
    }
    return TerminalBackground;
}
