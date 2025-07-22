/***************************************************************/
/*                                                             */
/*  QUEUE.C                                                    */
/*                                                             */
/*  Queue up reminders for subsequent execution.               */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <sys/select.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "types.h"
#include "globals.h"
#include "err.h"
#include "protos.h"

#undef USE_INOTIFY
#if defined(HAVE_SYS_INOTIFY_H) && defined(HAVE_INOTIFY_INIT1)
#define USE_INOTIFY 1
#include <sys/inotify.h>

int watch_fd = -1;
static void consume_inotify_events(int fd);
static int setup_inotify_watch(void);
#endif

/* List structure for holding queued reminders */
typedef struct queuedrem {
    struct queuedrem *next;
    int typ;
    int RunDisabled;
    int ntrig;
    char const *text;
    char const *fname;
    int lineno;
    int lineno_start;
    char passthru[PASSTHRU_LEN+1];
    char sched[VAR_NAME_LEN+1];
    Trigger t;
    TimeTrig tt;
    int red, green, blue;
} QueuedRem;

/* Global variables */

static QueuedRem *QueueHead = NULL;
static time_t FileModTime;
static struct stat StatBuf;

static void CheckInitialFile (void);
static int CalculateNextTime (QueuedRem *q);
static QueuedRem *FindNextReminder (void);
static int CalculateNextTimeUsingSched (QueuedRem *q);
static void ServerWait (struct timeval *sleep_tv);
static void reread (void);
static void PrintQueue(void);

static void chomp(DynamicBuffer *buf)
{
    char *s = DBufValue(buf);
    int l = DBufLen(buf);
    while (l) {
        if (s[l-1] == '\n') {
            s[l-1] = 0;
            DBufLen(buf)--;
            l--;
        } else {
            break;
        }
    }
}

static char const *SimpleTimeNoSpace(int tim)
{
    char *s = (char *) SimpleTime(tim);
    if (s && *s) {
        size_t l = strlen(s);
        if (l > 0 && s[l-1] == ' ') {
            s[l-1] = 0;
        }
    }
    return s;
}

static void del_reminder(QueuedRem const *qid)
{
    QueuedRem *q = QueueHead;
    QueuedRem *next;
    if (!q) {
        return;
    }
    if (q == qid) {
        QueueHead = q->next;
        if (q->text) free((void *) q->text);
        FreeTrig(&(q->t));
        free(q);
        return;
    }
    while(q->next) {
        next = q->next;
        if (q->next == qid) {
            q->next = q->next->next;
            if (next->text) free((void *) next->text);
            FreeTrig(&(next->t));
            free(next);
            return;
        }
        q = q->next;
    }
}

static void del_reminder_ul(unsigned long qid) {
    del_reminder((QueuedRem *) qid);
}

/***************************************************************/
/*                                                             */
/*  QueueReminder                                              */
/*                                                             */
/*  Put the reminder on a queue for later, if queueing is      */
/*  enabled.                                                   */
/*                                                             */
/***************************************************************/
int QueueReminder(ParsePtr p, Trigger *trig,
                  TimeTrig const *tim, char const *sched)
{
    QueuedRem *qelem;

    if (DontQueue ||
        trig->noqueue ||
        tim->ttime == NO_TIME ||
        trig->typ == CAL_TYPE ||
        tim->ttime < MinutesPastMidnight(0) ||
        ((trig->typ == RUN_TYPE) && RunDisabled)) return OK;

    qelem = NEW(QueuedRem);
    if (!qelem) {
        return E_NO_MEM;
    }
    qelem->red = DefaultColorR;
    qelem->green = DefaultColorG;
    qelem->blue = DefaultColorB;
    qelem->text = StrDup(p->pos);  /* Guaranteed that parser is not nested. */
    if (!qelem->text) {
        free(qelem);
        return E_NO_MEM;
    }
    qelem->fname = GetCurrentFilename();
    qelem->lineno = LineNo;
    qelem->lineno_start = LineNoStart;
    NumQueued++;
    qelem->typ = trig->typ;
    strcpy(qelem->passthru, trig->passthru);
    qelem->tt = *tim;
    qelem->t = *trig;

    /* Take over infos */
    trig->infos = NULL;

    DBufInit(&(qelem->t.tags));
    DBufPuts(&(qelem->t.tags), DBufValue(&(trig->tags)));
    if (SynthesizeTags) {
        AppendTag(&(qelem->t.tags), SynthesizeTag());
    }
    qelem->next = QueueHead;
    qelem->RunDisabled = RunDisabled;
    qelem->ntrig = 0;
    strcpy(qelem->sched, sched);
    QueueHead = qelem;
    return OK;
}

static void
maybe_close(int fd)
{
    int new_fd;
    /* Don't close descriptors connected to a TTY, except for stdin */
    if (fd && isatty(fd)) return;

    (void) close(fd);
    if (fd != STDIN_FILENO) {
        new_fd = open("/dev/null", O_WRONLY);
    } else {
        new_fd = open("/dev/null", O_RDONLY);
    }

    /* If the open failed... well... not much we can do */
    if (new_fd < 0) return;

    /* If we got back the same fd as what we just closed, aces! */
    if (fd == new_fd) return;

    (void) dup2(new_fd, fd);
    (void) close(new_fd);
}

static void
SigContHandler(int d)
{
    UNUSED(d);
}

static void
print_num_queued(void)
{
        int nqueued = 0;
        QueuedRem *q = QueueHead;
        while(q) {
            if (q->tt.nexttime != NO_TIME) {
                nqueued++;
            }
            q = q->next;
        }
        if (DaemonJSON) {
            printf("{");
            PrintJSONKeyPairString("response", "queued");
            PrintJSONKeyPairInt("nqueued", nqueued);
            printf("\"command\":\"STATUS\"}\n");
        } else {
            printf("NOTE queued %d\n", nqueued);
        }
        fflush(stdout);
}

/***************************************************************/
/*                                                             */
/*  HandleQueuedReminders                                      */
/*                                                             */
/*  Handle the issuing of queued reminders in the background   */
/*                                                             */
/***************************************************************/
void HandleQueuedReminders(void)
{
    QueuedRem *q = QueueHead;
    int TimeToSleep;
    unsigned SleepTime;
    Parser p;
    struct timeval tv;
    struct timeval sleep_tv;
    struct sigaction sa;
    char qid[64];

    /* Disable any potential pending SIGALRMs */
    alarm(0);

    /* Un-limit execution time */
    unlimit_execution_time();

    /* Turn off sorting -- otherwise, TriggerReminder has no effect! */
    SortByDate = 0;

    /* We don't need to keep the dedupe table around */
    ClearDedupeTable();

    /* If we are not connected to a tty, then we must close the
     * standard file descriptors. This is to prevent someone
     * doing:
     *          remind file | <filter> | >log
     * and have <filter> hung because the child (us) is still
     * connected to it. This means the only commands that will be
     * processed correctly are RUN commands, provided they mail
     * the result back or use their own resource (as a window).
     */
    if (ShouldFork) {
        maybe_close(STDIN_FILENO);
        maybe_close(STDOUT_FILENO);
        maybe_close(STDERR_FILENO);
    }

    /* If we're a daemon, get the mod time of initial file */
    if (Daemon > 0) {
        if (stat(InitialFile, &StatBuf)) {
            fprintf(ErrFp, tr("Cannot stat %s - not running as daemon!"),
                    InitialFile);
            fprintf(ErrFp, "\n");
            Daemon = 0;
        } else FileModTime = StatBuf.st_mtime;
    }

    /* Initialize the queue - initialize all the entries time of issue */

    while (q) {
        q->tt.nexttime = MinutesPastMidnight(1) - 1;
        q->tt.nexttime = CalculateNextTime(q);
        q = q->next;
    }

    if (ShouldFork || Daemon) {
        sa.sa_handler = SigIntHandler;
        sa.sa_flags = SA_RESTART;
        sigemptyset(&sa.sa_mask);
        (void) sigaction(SIGINT, &sa, NULL);
        sa.sa_handler = SigContHandler;
        (void) sigaction(SIGCONT, &sa, NULL);
    }

#ifdef USE_INOTIFY
    watch_fd = setup_inotify_watch();
#endif
    /* Sit in a loop, issuing reminders when necessary */
    while(1) {
        q = FindNextReminder();

        /* If no more reminders to issue, we're done unless we're a daemon. */
        if (!q && !Daemon) break;

        if (Daemon && !q) {
            if (IsServerMode()) {
                /* Sleep until midnight */
                TimeToSleep = MINUTES_PER_DAY*60 - SystemTime(1);
            } else {
                TimeToSleep = 60*Daemon;
            }
        } else {
            TimeToSleep = q->tt.nexttime * 60L - SystemTime(1);
        }

        while (TimeToSleep > 0L) {
            SleepTime = TimeToSleep;

            if (Daemon > 0 && SleepTime > (unsigned int) 60*Daemon) {
                SleepTime = 60*Daemon;
            }

            if (IsServerMode()) {
                /* Wake up on the next exact minute */
                gettimeofday(&tv, NULL);
                sleep_tv.tv_sec = 60 - (tv.tv_sec % 60);
                if (tv.tv_usec != 0 && sleep_tv.tv_sec != 0) {
                    sleep_tv.tv_sec--;
                    sleep_tv.tv_usec = 1000000 - tv.tv_usec;
                } else {
                    sleep_tv.tv_usec = 0;
                }
                ServerWait(&sleep_tv);
                /* A DEL command might have deleted our queued reminder! */
                q = FindNextReminder();
            } else {
                sleep(SleepTime);
            }

            if (GotSigInt()) {
                PrintQueue();
            }

            /* If not in daemon mode and day has rolled around,
               exit -- not much we can do. */
            if (!Daemon) {
                int y, m, d;
                if (RealToday != SystemDate(&y, &m, &d)) {
                        exit(EXIT_SUCCESS);
                }
            }

            if (Daemon > 0 && SleepTime) {
                CheckInitialFile();
            }

            if (Daemon && !q) {
                if (IsServerMode()) {
                    /* Sleep until midnight */
                    TimeToSleep = MINUTES_PER_DAY*60 - SystemTime(1);
                } else {
                    TimeToSleep = 60*Daemon;
                }
            } else {
                TimeToSleep = q->tt.nexttime * 60L - SystemTime(1);
            }

        }

        /* Do NOT trigger the reminder if tt.nexttime is more than a
           minute in the past.  This can happen if the clock is
           changed or a laptop awakes from hibernation.
           However, DO trigger if tt.nexttime == tt.ttime and we're
           within MaxLateTrigger minutes so all
           queued reminders are triggered at least once. */
        if ((SystemTime(1) - (q->tt.nexttime * 60) <= 60) ||
            (q->tt.nexttime == q->tt.ttime &&
             (MaxLateMinutes == 0 || SystemTime(1) - (q->tt.nexttime * 60) <= 60 * MaxLateMinutes))) {
            /* Trigger the reminder */
            CreateParser(q->text, &p);
            RunDisabled = q->RunDisabled;
            if (IsServerMode() && q->typ != RUN_TYPE) {
                if (DaemonJSON) {
                    printf("{\"response\":\"reminder\",");
                    if (TestMode) {
                        snprintf(qid, sizeof(qid), "42424242");
                    } else {
                        snprintf(qid, sizeof(qid), "%lx", (unsigned long) q);
                    }
                    PrintJSONKeyPairString("qid", qid);
                    PrintJSONKeyPairString("ttime", SimpleTimeNoSpace(q->tt.ttime));
                    PrintJSONKeyPairString("now", SimpleTimeNoSpace(MinutesPastMidnight(1)));
                    if (q->t.infos) {
                        WriteJSONInfoChain(q->t.infos);
                    }
                    PrintJSONKeyPairString("tags", DBufValue(&q->t.tags));
                } else {
                    printf("NOTE reminder %s",
                           SimpleTime(q->tt.ttime));
                    printf("%s", SimpleTime(MinutesPastMidnight(1)));
                    if (!*DBufValue(&q->t.tags)) {
                        printf("*\n");
                    } else {
                        printf("%s\n", DBufValue(&(q->t.tags)));
                    }
                }
            }

            /* Set up global variables so some functions like trigdate()
               and trigtime() work correctly                             */
            SaveAllTriggerInfo(&(q->t), &(q->tt), DSEToday, q->tt.ttime, 1);
            SetCurrentFilename(q->fname);
            DefaultColorR = q->red;
            DefaultColorG = q->green;
            DefaultColorB = q->blue;
            /* Make a COPY of q->t because TriggerReminder can change q->t.typ */
            Trigger tcopy = q->t;

            if (DaemonJSON) {
                DynamicBuffer out;
                DBufInit(&out);
                (void) TriggerReminder(&p, &tcopy, &q->tt, DSEToday, 1, &out);
                if (q->typ != RUN_TYPE) {
                    printf("\"body\":\"");
                    chomp(&out);
                    PrintJSONString(DBufValue(&out));
                    printf("\"}\n");
                }
                DBufFree(&out);
            } else {
                (void) TriggerReminder(&p, &tcopy, &q->tt, DSEToday, 1, NULL);
            }
            if (IsServerMode() && !DaemonJSON && q->typ != RUN_TYPE) {
                printf("NOTE endreminder\n");
            }
            fflush(stdout);
            DestroyParser(&p);
        }

        /* Calculate the next trigger time */
        q->tt.nexttime = CalculateNextTime(q);

        if (q->tt.nexttime != NO_TIME) {
            /* If trigger time is way in the past because computer has been
               suspended or hibernated, remove from queue */
            if (q->tt.ttime < MinutesPastMidnight(1) - MaxLateMinutes &&
                q->tt.nexttime < MinutesPastMidnight(1) - MaxLateMinutes) {
                q->tt.nexttime = NO_TIME;
            }
        }

        /* If queued reminder has expired, actually remove it from queue
           and update status */
        if (q->tt.nexttime == NO_TIME) {
            del_reminder(q);
            if (IsServerMode()) {
                print_num_queued();
            }
        }
    }
    exit(EXIT_SUCCESS);
}


/***************************************************************/
/*                                                             */
/*  CalculateNextTime                                          */
/*                                                             */
/*  Calculate the next time when a reminder should be issued.  */
/*  Return NO_TIME if reminder expired.                        */
/*  Strategy is:  If a sched() function is defined, call it.   */
/*  Otherwise, use AT time with delta and rep.  If sched()     */
/*  fails, revert to AT with delta and rep.                    */
/*                                                             */
/***************************************************************/
static int CalculateNextTime(QueuedRem *q)
{
    int tim = q->tt.ttime;
    int rep = q->tt.rep;
    int delta = q->tt.delta;
    int curtime = q->tt.nexttime+1;
    int r;

/* Increment number of times this one has been triggered */
    q->ntrig++;
    if (q->sched[0]) {
        r = CalculateNextTimeUsingSched(q);
        if (r != NO_TIME) return r;
    }
    if (delta == NO_DELTA) {
        if (tim < curtime) {
            return NO_TIME;
        } else {
            return tim;
        }
    }

    tim -= delta;
    if (rep == NO_REP) rep = delta;
    if (tim < curtime) tim += ((curtime - tim) / rep) * rep;
    if (tim < curtime) tim += rep;
    if (tim > q->tt.ttime) tim = q->tt.ttime;
    if (tim < curtime) return NO_TIME; else return tim;
}

/***************************************************************/
/*                                                             */
/*  FindNextReminder                                           */
/*                                                             */
/*  Find the next reminder to trigger                          */
/*                                                             */
/***************************************************************/
static QueuedRem *FindNextReminder(void)
{
    QueuedRem *q = QueueHead;
    QueuedRem *ans = NULL;

    while (q) {
        if (q->tt.nexttime != NO_TIME) {
            if (!ans) ans = q;
            else if (q->tt.nexttime < ans->tt.nexttime) ans = q;
        }

        q = q->next;
    }
    return ans;
}


/***************************************************************/
/*                                                             */
/* PrintQueue                                                  */
/*                                                             */
/* For debugging: Print queue contents to STDOUT               */
/*                                                             */
/***************************************************************/
static
void PrintQueue(void)
{
    QueuedRem *q = QueueHead;

    printf("Contents of AT queue:%s", NL);

    while (q) {
        if (q->tt.nexttime != NO_TIME) {
            printf("Trigger: %02d%c%02d  Activate: %02d%c%02d  Rep: %d  Delta: %d  Sched: %s",
                   q->tt.ttime / 60, TimeSep, q->tt.ttime % 60,
                   q->tt.nexttime / 60, TimeSep, q->tt.nexttime % 60,
                   q->tt.rep, q->tt.delta, q->sched);
            if (*q->sched) printf("(%d)", q->ntrig+1);
            printf("%s", NL);
            printf("Text: %s %s%s%s%s%s", ((q->typ == MSG_TYPE) ? "MSG" :
                                       ((q->typ == MSF_TYPE) ? "MSF" : 
                                        ((q->typ == RUN_TYPE) ? "RUN" : "SPECIAL"))),
                   q->passthru,
                   (*(q->passthru)) ? " " : "",
                   q->text,
                   NL, NL);
        }
        q = q->next;
    }
    printf(NL);
    printf("To terminate program, send SIGQUIT (probably Ctrl-\\ on the keyboard.)%s", NL);
}

/***************************************************************/
/*                                                             */
/*  CheckInitialFile                                           */
/*                                                             */
/*  If the initial file has been modified, then restart the    */
/*  daemon.                                                    */
/*                                                             */
/***************************************************************/
static void CheckInitialFile(void)
{
    /* If date has rolled around, or file has changed, spawn a new version. */
    time_t tim = FileModTime;
    int y, m, d;
#ifdef USE_INOTIFY
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    int n;
#endif

#ifdef USE_INOTIFY
    /* If there are any inotify events, reread */
    if (watch_fd >= 0) {
        while(1) {
            n = read(watch_fd, buf, sizeof(buf));
            if (n < 0 && errno == EINTR) continue;
            if (n > 0) {
                close(watch_fd);
                reread();
            }
            break;
        }
    }
#endif
    if (stat(InitialFile, &StatBuf) == 0) tim = StatBuf.st_mtime;
    if (tim != FileModTime ||
        RealToday != SystemDate(&y, &m, &d)) {
        reread();
    }
}

/***************************************************************/
/*                                                             */
/*  CalculateNextTimeUsingSched                                */
/*                                                             */
/*  Call the scheduling function.                              */
/*                                                             */
/***************************************************************/
static int CalculateNextTimeUsingSched(QueuedRem *q)
{
    /* Use LineBuffer for temp. string storage. */
    int r;
    Value v;
    char const *s;
    int LastTime = -1;
    int ThisTime;

    if (UserFuncExists(q->sched) != 1) {
        q->sched[0] = 0;
        return NO_TIME;
    }

    RunDisabled = q->RunDisabled;  /* Don't want weird scheduling functions
                                     to be a security hole!                */
    while(1) {
        char exprBuf[VAR_NAME_LEN+32];
        snprintf(exprBuf, sizeof(exprBuf), "%s(%d)", q->sched, q->ntrig);
        s = exprBuf;
        r = EvalExpr(&s, &v, NULL);
        if (r) {
            q->sched[0] = 0;
            return NO_TIME;
        }
        if (v.type == TIME_TYPE) {
            ThisTime = v.v.val;
        } else if (v.type == INT_TYPE) {
            if (v.v.val > 0)
                if (LastTime >= 0) {
                    ThisTime = LastTime + v.v.val;
                } else {
                    ThisTime = q->tt.nexttime + v.v.val;
                }
            else
                ThisTime = q->tt.ttime + v.v.val;

        } else {
            DestroyValue(v);
            q->sched[0] = 0;
            return NO_TIME;
        }
        if (ThisTime < 0) ThisTime = 0;        /* Can't be less than 00:00 */
        if (ThisTime > (MINUTES_PER_DAY-1)) ThisTime = (MINUTES_PER_DAY-1);  /* or greater than 11:59 */
        if (DebugFlag & DB_PRTEXPR) {
            fprintf(ErrFp, "SCHED: Considering %02d%c%02d\n",
                    ThisTime / 60, TimeSep, ThisTime % 60);
        }
        if (ThisTime > q->tt.nexttime) return ThisTime;
        if (ThisTime <= LastTime) {
            q->sched[0] = 0;
            return NO_TIME;
        }
        LastTime = ThisTime;
        q->ntrig++;
    }
}

/* Dump the queue in JSON format */
static void
json_queue(QueuedRem const *q)
{
    int done = 0;
    if (DaemonJSON) {
        printf("{\"response\":\"queue\",\"queue\":");
    }
    printf("[");
    char idbuf[64];
    while(q) {
        if (q->tt.nexttime == NO_TIME) {
            q = q->next;
            continue;
        }
        if (done) {
            printf(",");
        }
        done = 1;
        printf("{");
        WriteJSONTrigger(&(q->t), 1, DSEToday);
        WriteJSONTimeTrigger(&(q->tt));
        if (TestMode) {
            snprintf(idbuf, sizeof(idbuf), "42424242");
        } else {
            snprintf(idbuf, sizeof(idbuf), "%lx", (unsigned long) q);
        }
        PrintJSONKeyPairString("qid", idbuf);
        PrintJSONKeyPairInt("rundisabled", q->RunDisabled);
        PrintJSONKeyPairInt("ntrig", q->ntrig);
        PrintJSONKeyPairString("filename", q->fname);
        PrintJSONKeyPairInt("lineno", q->lineno);
        if (q->lineno_start != q->lineno) {
            PrintJSONKeyPairInt("lineno_start", q->lineno_start);
        }
        switch(q->typ) {
        case NO_TYPE: PrintJSONKeyPairString("type", "NO_TYPE"); break;
        case MSG_TYPE: PrintJSONKeyPairString("type", "MSG_TYPE"); break;
        case RUN_TYPE: PrintJSONKeyPairString("type", "RUN_TYPE"); break;
        case CAL_TYPE: PrintJSONKeyPairString("type", "CAL_TYPE"); break;
        case SAT_TYPE: PrintJSONKeyPairString("type", "SAT_TYPE"); break;
        case PS_TYPE: PrintJSONKeyPairString("type", "PS_TYPE"); break;
        case PSF_TYPE: PrintJSONKeyPairString("type", "PSF_TYPE"); break;
        case MSF_TYPE: PrintJSONKeyPairString("type", "MSF_TYPE"); break;
        case PASSTHRU_TYPE:
            PrintJSONKeyPairString("type", "PASSTHRU_TYPE");
            PrintJSONKeyPairString("passthru", q->passthru);
            break;
        default: PrintJSONKeyPairString("type", "?"); break;
        }

        /* Last one is a special case - no trailing comma */
        printf("\"");
        PrintJSONString("body");
        printf("\":\"");
        if (q->text) {
            PrintJSONString(q->text);
        } else {
            PrintJSONString("");
        }
        printf("\"}");
        q = q->next;
    }
    printf("]");
    if (DaemonJSON) {
        printf(",\"command\":\"QUEUE\"}\n");
    } else {
        printf("\n");
    }
}

/***************************************************************/
/*                                                             */
/*  ServerWait                                                 */
/*                                                             */
/*  Sleep or read command from stdin in server mode            */
/*                                                             */
/***************************************************************/
static void ServerWait(struct timeval *sleep_tv)
{
    fd_set readSet;
    int retval;
    int y, m, d;
    int max = 1;
    char cmdLine[256];
    char *s;
    int r;
    DynamicBuffer tx;

    DBufInit(&tx);

    FD_ZERO(&readSet);
    FD_SET(0, &readSet);

#ifdef USE_INOTIFY
    if (watch_fd >= 0) {
        FD_SET(watch_fd, &readSet);
        if (watch_fd > max-1)
            max = watch_fd+1;
    }
#endif
    retval = select(max, &readSet, NULL, NULL, sleep_tv);

    /* If date has rolled around, restart */
    if (RealToday != SystemDate(&y, &m, &d)) {
        if (DaemonJSON) {
            printf("{\"response\":\"newdate\"}\n{\"response\":\"reread\",\"command\":\"newdate\"}\n");
        } else {
            printf("NOTE newdate\nNOTE reread\n");
        }
        fflush(stdout);
        reread();
    }

    /* If nothing readable or interrupted system call, return */
    if (retval <= 0) return;

    /* If inotify watch descriptor is readable, handle it */
#ifdef USE_INOTIFY
    if (watch_fd >= 0) {
        if (FD_ISSET(watch_fd, &readSet)) {
            consume_inotify_events(watch_fd);
            if (DaemonJSON) {
                printf("{\"response\":\"reread\",\"command\":\"inotify\"}\n");
            } else {
                /* In deprecated server mode, we need to spit out
                   a NOTE newdate to force the front-end to redraw
                   the calendar */
                printf("NOTE newdate\nNOTE reread\n");
            }
            fflush(stdout);
            reread();
        }
    }
#endif
    /* If stdin not readable, return */
    if (!FD_ISSET(0, &readSet)) return;

    /* If EOF on stdin, exit */
    if (feof(stdin)) {
        exit(EXIT_SUCCESS);
    }

    /* Read a line using read() one char at a time to avoid resetting
     * readability if we get two commands quickly */

    s = cmdLine;
    *s = 0;
    while (1) {
        r = read(fileno(stdin), s, 1);
        if (r == 0) {
            /* EOF */
            exit(EXIT_SUCCESS);
        }
        if (r != 1) {
            /* Error? */
            if (errno == EINTR) {
                continue;
            }
            exit(EXIT_FAILURE);
        }
        *(s+1) = 0;
        if (*s == '\n') {
            break;
        }
        if ((size_t) (s - cmdLine) >= sizeof(cmdLine)-1) {
            break;
        }
        s++;
    }

    if (!strcmp(cmdLine, "EXIT\n")) {
        exit(EXIT_SUCCESS);
    } else if (!strcmp(cmdLine, "STATUS\n")) {
        print_num_queued();
    } else if (!strcmp(cmdLine, "QUEUE\n")) {
        if (DaemonJSON) {
            json_queue(QueueHead);
        } else {
            printf("NOTE queue\n");
            QueuedRem *q = QueueHead;
            while (q) {
                if (q->tt.nexttime != NO_TIME) {
                    switch (q->typ) {
                    case NO_TYPE: printf("NO_TYPE"); break;
                    case MSG_TYPE: printf("MSG_TYPE"); break;
                    case RUN_TYPE: printf("RUN_TYPE"); break;
                    case CAL_TYPE: printf("CAL_TYPE"); break;
                    case SAT_TYPE: printf("SAT_TYPE"); break;
                    case PS_TYPE: printf("PS_TYPE"); break;
                    case PSF_TYPE: printf("PSF_TYPE"); break;
                    case MSF_TYPE: printf("MSF_TYPE"); break;
                    case PASSTHRU_TYPE: printf("PASSTHRU_TYPE"); break;
                    default: printf("?"); break;
                    }
                    printf(" RunDisabled=%d ntrig=%d ttime=%02d:%02d nexttime=%02d:%02d delta=%d rep=%d duration=%d ", q->RunDisabled, q->ntrig, q->tt.ttime/60, q->tt.ttime % 60, q->tt.nexttime / 60, q->tt.nexttime % 60, q->tt.delta, (q->tt.rep != NO_TIME ? q->tt.rep : -1), (q->tt.duration != NO_TIME ? q->tt.duration : -1));
                    printf("%s %s %s\n",
                           (q->passthru[0] ? q->passthru : "*"),
                           (q->sched[0] ? q->sched : "*"),
                           q->text ? q->text : "NULL");
                }
                q = q->next;
            }
            printf("NOTE endqueue\n");
        }
        fflush(stdout);
    } else if (!strcmp(cmdLine, "JSONQUEUE\n")) {
        if (!DaemonJSON) {
            printf("NOTE JSONQUEUE\n");
        }
        json_queue(QueueHead);
        if (!DaemonJSON) {
            printf("NOTE ENDJSONQUEUE\n");
        }
        fflush(stdout);
    } else if (DaemonJSON && !strncmp(cmdLine, "TRANSLATE ", 10)) {
        /* Cut off the trailing "\n" */
        if (*(cmdLine + strlen(cmdLine)-1) == '\n') {
            *(cmdLine + strlen(cmdLine)-1) = 0;
        }

        r = GetTranslatedStringTryingVariants(cmdLine+10, &tx);

        /* Output NOTHING if there's no translation */
        if (r) {
            printf("{");
            PrintJSONKeyPairString("response", "translate");
            printf("\"translation\":{\"");
            PrintJSONString(cmdLine+10);
            printf("\":\"");
            PrintJSONString(DBufValue(&tx));
            DBufFree(&tx);
            printf("\"},");
            printf("\"command\":\"TRANSLATE\"}\n");
            fflush(stdout);
        }
    } else if (!strcmp(cmdLine, "TRANSLATE_DUMP\n")) {
        if (!DaemonJSON) {
            printf("NOTE TRANSLATE_DUMP\n");
        } else {
            printf("{");
            PrintJSONKeyPairString("response", "translate_dump");
            printf("\"table\":");
        }
        DumpTranslationTable(stdout, 1);
        if (!DaemonJSON) {
            printf("\nNOTE ENDTRANSLATE_DUMP\n");
        } else {
            printf(",\"command\":\"TRANSLATE_DUMP\"}\n");
        }
        fflush(stdout);
    } else if (!strcmp(cmdLine, "REREAD\n")) {
        if (DaemonJSON) {
            printf("{\"response\":\"reread\",\"command\":\"REREAD\"}\n");
        } else {
            printf("NOTE reread\n");
        }
        fflush(stdout);
        reread();
    } else if (!strncmp(cmdLine, "DEL ", 4)) {
        unsigned long qid;
        if (sscanf(cmdLine, "DEL %lx", &qid) == 1) {
            del_reminder_ul(qid);
        }
        print_num_queued();
        fflush(stdout);
    } else {
        if (DaemonJSON) {
            size_t l = strlen(cmdLine);
            if (l && cmdLine[l-1] == '\n') {
                cmdLine[l-1] = 0;
            }
            printf("{\"response\":\"error\",\"error\":\"Unknown command\",\"command\":\"");
            PrintJSONString(cmdLine);
            printf("\"}\n");
        } else {
            printf("ERR Invalid daemon command: %s", cmdLine);
        }
        fflush(stdout);
    }
}

/***************************************************************/
/*                                                             */
/*  reread                                                     */
/*                                                             */
/*  Restarts Remind if date rolls over or REREAD cmd received  */
/*                                                             */
/***************************************************************/
static void reread(void)
{
    execvp(ArgV[0], (char **) ArgV);
}

#ifdef USE_INOTIFY
static void consume_inotify_events(int fd)
{
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    int n;

    struct timespec sleeptime;

    int slept = 0;
    /* Consume all the inotify events */
    while(1) {
        n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            /* Something new since we slept */
            slept = 0;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (slept) {
                /* Nothing new since we slept */
                return;
            }
            slept = 1;
            /* HACK: sleep for 0.2 seconds to let multiple events queue up so we
               only do a single reread */
            sleeptime.tv_sec = 0;
            sleeptime.tv_nsec = 200000000;
            nanosleep(&sleeptime, NULL);
        }
    }
}

static int setup_inotify_watch(void)
{
    int fd;

    /* Don't inotify_watch stdin */
    if (!strcmp(InitialFile, "-")) {
        return -1;
    }

    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        return fd;
    }
    if (inotify_add_watch(fd, InitialFile, IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

#endif
