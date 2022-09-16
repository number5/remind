/***************************************************************/
/*                                                             */
/*  CALENDAR.C                                                 */
/*                                                             */
/*  The code for generating a calendar.                        */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2022 by Dianne Skoll                    */
/*                                                             */
/***************************************************************/

#define _XOPEN_SOURCE
#include "config.h"
#include "custom.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef REM_USE_WCHAR
#include <wctype.h>
#include <wchar.h>
#endif

#ifdef HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#include "lang.h"
#include "types.h"
#include "protos.h"
#include "expr.h"
#include "globals.h"
#include "err.h"
#include "md5.h"

/* Data structures used by the calendar */
typedef struct cal_entry {
    struct cal_entry *next;
    char *text;
    char *raw_text;
    char const *pos;
#ifdef REM_USE_WCHAR
    wchar_t *wc_text;
    wchar_t const *wc_pos;
#endif
    int is_color;
    int r, g, b;
    int time;
    int priority;
    DynamicBuffer tags;
    char passthru[PASSTHRU_LEN+1];
    int duration;
    char *filename;
    int lineno;
    Trigger trig;
    TimeTrig tt;
    int nonconst_expr;
    int if_depth;
} CalEntry;

/* Line-drawing sequences */
struct line_drawing {
  char const *graphics_on;
  char const *graphics_off;
  char *tlr, *bl, *tbl, *blr, *tblr, *tr, *tb, *br, *tbr, *tl, *lr;
};

static struct line_drawing NormalDrawing = {
  "", "", "+", "+", "+", "+", "+", "+", "|", "+", "+", "+", "-"
};

static struct line_drawing VT100Drawing = {
  "\x1B(0", "\x1B(B",
  "\x76", "\x6b", "\x75", "\x77", "\x6e", "\x6d", "\x78",
  "\x6c", "\x74", "\x6a", "\x71"
};

static int encoding_is_utf8 = 0;

static struct line_drawing UTF8Drawing = {
    "", "",
    "\xe2\x94\xb4",
    "\xe2\x94\x90",
    "\xe2\x94\xa4",
    "\xe2\x94\xac",
    "\xe2\x94\xbc",
    "\xe2\x94\x94",
    "\xe2\x94\x82",
    "\xe2\x94\x8c",
    "\xe2\x94\x9c",
    "\xe2\x94\x98",
    "\xe2\x94\x80"
};

static char *VT100Colors[2][2][2][2] /* [Br][R][G][B] */ = {
  {
  /*** DIM COLORS ***/
    {
      {
	/* 0, 0, 0 = Black   */ "\x1B[0;30m",
	/* 0, 0, 1 = Blue    */ "\x1B[0;34m"
      },
      {
	/* 0, 1, 0 = Green   */ "\x1B[0;32m",
	/* 0, 1, 1 = Cyan    */ "\x1B[0;36m"
      }
    },
    {
      {
	/* 1, 0, 0 = Red     */ "\x1B[0;31m",
	/* 1, 0, 1 = Magenta */ "\x1B[0;35m"
      },
      {
	/* 1, 1, 0 = Yellow  */ "\x1B[0;33m",
	/* 1, 1, 1 = White   */ "\x1B[0;37m"
      }
    }
  },
  {
  /*** BRIGHT COLORS ***/
    {
      {
	/* 0, 0, 0 = Grey    */ "\x1B[30;1m",
	/* 0, 0, 1 = Blue    */ "\x1B[34;1m"
      },
      {
	/* 0, 1, 0 = Green   */ "\x1B[32;1m",
	/* 0, 1, 1 = Cyan    */ "\x1B[36;1m"
      }
    },
    {
      {
	/* 1, 0, 0 = Red     */ "\x1B[31;1m",
	/* 1, 0, 1 = Magenta */ "\x1B[35;1m"
      },
      {
	/* 1, 1, 0 = Yellow  */ "\x1B[33;1m",
	/* 1, 1, 1 = White   */ "\x1B[37;1m"
      }
    }
  }
};

static char *VT100BGColors[2][2][2] /* [R][G][B] */ = {
  {
      {
          /* 0, 0, 0 = Black   */ "\x1B[0;40m",
          /* 0, 0, 1 = Blue    */ "\x1B[0;44m"
      },
      {
          /* 0, 1, 0 = Green   */ "\x1B[0;42m",
          /* 0, 1, 1 = Cyan    */ "\x1B[0;46m"
      }
  },
  {
      {
          /* 1, 0, 0 = Red     */ "\x1B[0;41m",
          /* 1, 0, 1 = Magenta */ "\x1B[0;45m"
      },
      {
          /* 1, 1, 0 = Yellow  */ "\x1B[0;43m",
          /* 1, 1, 1 = White   */ "\x1B[0;47m"
      }
  }
};

/* Moon phase icons in UTF-8 */
static char const *moonphase_emojis[] = {
    "\xF0\x9F\x8C\x91",
    "\xF0\x9F\x8C\x93",
    "\xF0\x9F\x8C\x95",
    "\xF0\x9F\x8C\x97"
};

static char moons[32][32];

static struct line_drawing *linestruct;
#define DRAW(x) fputs(linestruct->x, stdout)

struct xterm256_colors {
    int r;
    int g;
    int b;
};

/* Xterm support 256 different colors, numbered from 0 to 255.  The following
   table is a list of the [r, g, b] components of each Xterm color.  The
   data was obtained from https://jonasjacek.github.io/colors/ */
static struct xterm256_colors XTerm256Colors[] =
{
    {  0,   0,   0}, {128,   0,   0}, {  0, 128,   0}, {128, 128,   0},
    {  0,   0, 128}, {128,   0, 128}, {  0, 128, 128}, {192, 192, 192},
    {128, 128, 128}, {255,   0,   0}, {  0, 255,   0}, {255, 255,   0},
    {  0,   0, 255}, {255,   0, 255}, {  0, 255, 255}, {255, 255, 255},
    {  0,   0,   0}, {  0,   0,  95}, {  0,   0, 135}, {  0,   0, 175},
    {  0,   0, 215}, {  0,   0, 255}, {  0,  95,   0}, {  0,  95,  95},
    {  0,  95, 135}, {  0,  95, 175}, {  0,  95, 215}, {  0,  95, 255},
    {  0, 135,   0}, {  0, 135,  95}, {  0, 135, 135}, {  0, 135, 175},
    {  0, 135, 215}, {  0, 135, 255}, {  0, 175,   0}, {  0, 175,  95},
    {  0, 175, 135}, {  0, 175, 175}, {  0, 175, 215}, {  0, 175, 255},
    {  0, 215,   0}, {  0, 215,  95}, {  0, 215, 135}, {  0, 215, 175},
    {  0, 215, 215}, {  0, 215, 255}, {  0, 255,   0}, {  0, 255,  95},
    {  0, 255, 135}, {  0, 255, 175}, {  0, 255, 215}, {  0, 255, 255},
    { 95,   0,   0}, { 95,   0,  95}, { 95,   0, 135}, { 95,   0, 175},
    { 95,   0, 215}, { 95,   0, 255}, { 95,  95,   0}, { 95,  95,  95},
    { 95,  95, 135}, { 95,  95, 175}, { 95,  95, 215}, { 95,  95, 255},
    { 95, 135,   0}, { 95, 135,  95}, { 95, 135, 135}, { 95, 135, 175},
    { 95, 135, 215}, { 95, 135, 255}, { 95, 175,   0}, { 95, 175,  95},
    { 95, 175, 135}, { 95, 175, 175}, { 95, 175, 215}, { 95, 175, 255},
    { 95, 215,   0}, { 95, 215,  95}, { 95, 215, 135}, { 95, 215, 175},
    { 95, 215, 215}, { 95, 215, 255}, { 95, 255,   0}, { 95, 255,  95},
    { 95, 255, 135}, { 95, 255, 175}, { 95, 255, 215}, { 95, 255, 255},
    {135,   0,   0}, {135,   0,  95}, {135,   0, 135}, {135,   0, 175},
    {135,   0, 215}, {135,   0, 255}, {135,  95,   0}, {135,  95,  95},
    {135,  95, 135}, {135,  95, 175}, {135,  95, 215}, {135,  95, 255},
    {135, 135,   0}, {135, 135,  95}, {135, 135, 135}, {135, 135, 175},
    {135, 135, 215}, {135, 135, 255}, {135, 175,   0}, {135, 175,  95},
    {135, 175, 135}, {135, 175, 175}, {135, 175, 215}, {135, 175, 255},
    {135, 215,   0}, {135, 215,  95}, {135, 215, 135}, {135, 215, 175},
    {135, 215, 215}, {135, 215, 255}, {135, 255,   0}, {135, 255,  95},
    {135, 255, 135}, {135, 255, 175}, {135, 255, 215}, {135, 255, 255},
    {175,   0,   0}, {175,   0,  95}, {175,   0, 135}, {175,   0, 175},
    {175,   0, 215}, {175,   0, 255}, {175,  95,   0}, {175,  95,  95},
    {175,  95, 135}, {175,  95, 175}, {175,  95, 215}, {175,  95, 255},
    {175, 135,   0}, {175, 135,  95}, {175, 135, 135}, {175, 135, 175},
    {175, 135, 215}, {175, 135, 255}, {175, 175,   0}, {175, 175,  95},
    {175, 175, 135}, {175, 175, 175}, {175, 175, 215}, {175, 175, 255},
    {175, 215,   0}, {175, 215,  95}, {175, 215, 135}, {175, 215, 175},
    {175, 215, 215}, {175, 215, 255}, {175, 255,   0}, {175, 255,  95},
    {175, 255, 135}, {175, 255, 175}, {175, 255, 215}, {175, 255, 255},
    {215,   0,   0}, {215,   0,  95}, {215,   0, 135}, {215,   0, 175},
    {215,   0, 215}, {215,   0, 255}, {215,  95,   0}, {215,  95,  95},
    {215,  95, 135}, {215,  95, 175}, {215,  95, 215}, {215,  95, 255},
    {215, 135,   0}, {215, 135,  95}, {215, 135, 135}, {215, 135, 175},
    {215, 135, 215}, {215, 135, 255}, {215, 175,   0}, {215, 175,  95},
    {215, 175, 135}, {215, 175, 175}, {215, 175, 215}, {215, 175, 255},
    {215, 215,   0}, {215, 215,  95}, {215, 215, 135}, {215, 215, 175},
    {215, 215, 215}, {215, 215, 255}, {215, 255,   0}, {215, 255,  95},
    {215, 255, 135}, {215, 255, 175}, {215, 255, 215}, {215, 255, 255},
    {255,   0,   0}, {255,   0,  95}, {255,   0, 135}, {255,   0, 175},
    {255,   0, 215}, {255,   0, 255}, {255,  95,   0}, {255,  95,  95},
    {255,  95, 135}, {255,  95, 175}, {255,  95, 215}, {255,  95, 255},
    {255, 135,   0}, {255, 135,  95}, {255, 135, 135}, {255, 135, 175},
    {255, 135, 215}, {255, 135, 255}, {255, 175,   0}, {255, 175,  95},
    {255, 175, 135}, {255, 175, 175}, {255, 175, 215}, {255, 175, 255},
    {255, 215,   0}, {255, 215,  95}, {255, 215, 135}, {255, 215, 175},
    {255, 215, 215}, {255, 215, 255}, {255, 255,   0}, {255, 255,  95},
    {255, 255, 135}, {255, 255, 175}, {255, 255, 215}, {255, 255, 255},
    {  8,   8,   8}, { 18,  18,  18}, { 28,  28,  28}, { 38,  38,  38},
    { 48,  48,  48}, { 58,  58,  58}, { 68,  68,  68}, { 78,  78,  78},
    { 88,  88,  88}, { 98,  98,  98}, {108, 108, 108}, {118, 118, 118},
    {128, 128, 128}, {138, 138, 138}, {148, 148, 148}, {158, 158, 158},
    {168, 168, 168}, {178, 178, 178}, {188, 188, 188}, {198, 198, 198},
    {208, 208, 208}, {218, 218, 218}, {228, 228, 228}, {238, 238, 238}
};

/* Global variables */
static CalEntry *CalColumn[7];

static int ColSpaces;

static int DidAMonth;
static int DidADay;

static void ColorizeEntry(CalEntry const *e);
static void SortCol (CalEntry **col);
static void DoCalendarOneWeek (int nleft);
static void DoCalendarOneMonth (void);
static int WriteCalendarRow (void);
static void WriteWeekHeaderLine (void);
static void WritePostHeaderLine (void);
static void PrintLeft (char const *s, int width, char pad);
static void PrintCentered (char const *s, int width, char *pad);
static int WriteOneCalLine (void);
static int WriteOneColLine (int col);
static void GenerateCalEntries (int col);
static void WriteCalHeader (void);
static void WriteCalTrailer (void);
static int DoCalRem (ParsePtr p, int col);
static void WriteSimpleEntries (int col, int jul);
static void WriteTopCalLine (void);
static void WriteBottomCalLine (void);
static void WriteIntermediateCalLine (void);
static void WriteCalDays (void);

static void
send_lrm(void)
{
    /* Don't send LRM if SuppressLRM is set */
    if (SuppressLRM) {
        return;
    }
    /* Send a lrm control sequence if UseUTF8Chars is enabled
       or char encoding is UTF-8
    */
    if (UseUTF8Chars || encoding_is_utf8) {
        printf("\xE2\x80\x8E");
    }
}

static char const *
despace(char const *s)
{
    static char buf[256];

    char *t = buf;
    if (strlen(s) > sizeof(buf)-1) {
        /* Punt. :( */
        return s;
    }
    while (*s) {
        if (isspace(*s)) {
            *t++ = '_';
        } else {
            *t++ = *s;
        }
        s++;
    }
    *t = 0;
    return buf;
}

void PrintJSONChar(char c) {
    switch(c) {
    case '\b': printf("\\b"); break;
    case '\f': printf("\\f"); break;
    case '\n': printf("\\n"); break;
    case '\r': printf("\\r"); break;
    case '\t': printf("\\t"); break;
    case '"':  printf("\\\""); break;
    case '\\': printf("\\\\"); break;
    default: printf("%c", c);
    }
}

void PrintJSONString(char const *s)
{
    while (*s) {
	switch(*s) {
	case '\b': printf("\\b"); break;
	case '\f': printf("\\f"); break;
	case '\n': printf("\\n"); break;
	case '\r': printf("\\r"); break;
	case '\t': printf("\\t"); break;
	case '"':  printf("\\\""); break;
	case '\\': printf("\\\\"); break;
	default: printf("%c", *s);
	}
	s++;
    }
}

void PrintJSONKeyPairInt(char const *name, int val)
{
    printf("\"");
    PrintJSONString(name);
    printf("\":%d,", val);
}

void PrintJSONKeyPairString(char const *name, char const *val)
{
    /* If value is blank, skip it! */
    if (!val || !*val) {
	return;
    }

    printf("\"");
    PrintJSONString(name);
    printf("\":\"");
    PrintJSONString(val);
    printf("\",");
}

void PrintJSONKeyPairDate(char const *name, int jul)
{
    int y, m, d;
    if (jul == NO_DATE) {
	/* Skip it! */
	return;
    }
    FromJulian(jul, &y, &m, &d);
    printf("\"");
    PrintJSONString(name);
    printf("\":\"%04d-%02d-%02d\",", y, m+1, d);

}

void PrintJSONKeyPairDateTime(char const *name, int dt)
{
    int y, m, d, h, i, k;
    if (dt == NO_TIME) {
	/* Skip it! */
	return;
    }
    i = dt / MINUTES_PER_DAY;
    FromJulian(i, &y, &m, &d);
    k = dt % MINUTES_PER_DAY;
    h = k / 60;
    i = k % 60;
    printf("\"");
    PrintJSONString(name);
    printf("\":\"%04d-%02d-%02dT%02d:%02d\",", y, m+1, d, h, i);

}

void PrintJSONKeyPairTime(char const *name, int t)
{
    int h, i;
    if (t == NO_TIME) {
	/* Skip it! */
	return;
    }
    h = t / 60;
    i = t % 60;
    printf("\"");
    PrintJSONString(name);
    printf("\":\"%02d:%02d\",", h, i);

}

#ifdef REM_USE_WCHAR
static void PutWideChar(wchar_t const wc)
{
    char buf[MB_CUR_MAX+1];
    int len;

    len = wctomb(buf, wc);
    if (len > 0) {
	buf[len] = 0;
	fputs(buf, stdout);
    }
}
#endif

static char const *
get_month_abbrev(char const *mon)
{
    static char buf[80];
#ifndef REM_USE_WCHAR
    sprintf(buf, "%.3s", mon);
    return buf;
#else
    char *s;
    wchar_t tmp_buf[128];
    wchar_t *ws;
    int i;
    int len;

    *buf = 0;
    (void) mbstowcs(tmp_buf, mon, 127);
    ws = tmp_buf;
    s = buf;
    for (i=0; i<3; i++) {
        if (*ws) {
            len = wctomb(s, *ws);
            s += len;
            if (wcwidth(*ws) == 0) {
                i--;
            }
            ws++;
        } else {
            break;
        }
    }
    *s = 0;
    return buf;
#endif
}

static int make_wchar_versions(CalEntry *e)
{
#ifdef REM_USE_WCHAR
    size_t len;
    wchar_t *buf;
    len = mbstowcs(NULL, e->text, 0);
    if (len == (size_t) -1) return 0;

    buf = calloc(len+1, sizeof(wchar_t));
    if (!buf) return 0;

    (void) mbstowcs(buf, e->text, len+1);
    buf[len] = 0;
    e->wc_text = buf;
    e->wc_pos = buf;
    return 1;
#else
    return 1;
#endif
}

static void gon(void)
{
  printf("%s", linestruct->graphics_on);
}
static void goff(void)
{
  printf("%s", linestruct->graphics_off);
}

static void
ClampColor(int *r, int *g, int *b)
{
    if (TerminalBackground == TERMINAL_BACKGROUND_UNKNOWN) {
	/* No special clamping if terminal background is unknown */
	return;
    }
    if (TerminalBackground == TERMINAL_BACKGROUND_DARK) {
	if (*r <= 64 && *g <= 64 && *b <= 64) {
	    int max = *r;
	    double factor;
	    if (*g > max) max = *g;
	    if (*b > max) max = *b;
	    if (max == 0) {
		*r = 65;
		*g = 65;
		*b = 65;
		return;
	    }
	    factor = 65.0 / (double) max;
	    *r = (int) (factor * (double) *r);
	    *g = (int) (factor * (double) *g);
	    *b = (int) (factor * (double) *b);
	}
	return;
    }
    if (TerminalBackground == TERMINAL_BACKGROUND_LIGHT) {
	if (*r > 191 && *g > 191 && *b > 191) {
	    int min = *r;
	    if (*g < min) min = *g;
	    if (*b < min) min = *b;
	    double factor = 192.0 / (double) min;
	    *r = (int) (factor * (double) *r);
	    *g = (int) (factor * (double) *g);
	    *b = (int) (factor * (double) *b);
	}
    }
}

char const *
Decolorize(int r, int g, int b)
{
    if (!strcmp(Colorize(r, g, b, 0), "")) {
	return "";
    }
    return "\x1B[0m";
}

static char const *
Colorize256(int r, int g, int b, int bg)
{
    static char buf[40];
    int best = -1;
    int best_dist = 0;
    int dist;
    struct xterm256_colors *cur;
    size_t i;

    ClampColor(&r, &g, &b);
    for (i=0; i<(sizeof(XTerm256Colors) / sizeof(XTerm256Colors[0])); i++) {
	cur = &XTerm256Colors[i];
	dist = ((r - cur->r) * (r - cur->r)) +
	    ((b - cur->b) * (b - cur->b)) +
	    ((g - cur->g) * (g - cur->g));
	if (best == -1 || dist < best_dist) {
	    best_dist = dist;
	    best = (int) i;
	}
    }
    cur = &XTerm256Colors[best];
    if (bg) {
        sprintf(buf, "\x1B[48;5;%dm", best);
    } else {
        sprintf(buf, "\x1B[38;5;%dm", best);
    }
    return buf;
}

static char const *
ColorizeTrue(int r, int g, int b, int bg)
{
    static char buf[40];
    ClampColor(&r, &g, &b);
    if (bg) {
        sprintf(buf, "\x1B[48;2;%d;%d;%dm", r, g, b);
    } else {
        sprintf(buf, "\x1B[38;2;%d;%d;%dm", r, g, b);
    }
    return buf;
}

char const *
Colorize(int r, int g, int b, int bg)
{
    int bright = 0;

    if (UseTrueColors) {
	return ColorizeTrue(r, g, b, bg);
    }

    if (Use256Colors) {
	return Colorize256(r, g, b, bg);
    }

    if (r > 128 || g > 128 || b > 128) {
	bright = 1;
    }
    if (r > 64) r = 1;
    else r = 0;
    if (g > 64) g = 1;
    else g = 0;
    if (b > 64) b = 1;
    else b = 0;

    if (TerminalBackground == TERMINAL_BACKGROUND_DARK && !bg) {
	/* Convert black-on-black to grey */
	if (!r && !g && !b) return VT100Colors[1][0][0][0];
    }
    if (TerminalBackground == TERMINAL_BACKGROUND_LIGHT && !bg) {
	/* Convert white-on-white to grey */
	if (r && g && b) return VT100Colors[1][0][0][0];
    }
    if (bg) {
        return VT100BGColors[r][g][b];
    } else {
        return VT100Colors[bright][r][g][b];
    }
}

static void ColorizeEntry(CalEntry const *e)
{
    printf("%s", Colorize(e->r, e->g, e->b, 0));
}

static int
ComputeCalWidth(int x)
{
    struct winsize w;
    if (x >= 71) {
	/* Has been set with -w option */
	return x;
    }
    if (!isatty(STDOUT_FILENO)) {
	/* Output is not a TTY... assume 80 */
	return 80;
    }
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) < 0) {
	return 80;
    }
    if (w.ws_col < 71) {
	return 71;
    }
    return w.ws_col;
}

static void
InitMoons(void)
{
    int i;
    /* Initialize the moon array */
    for (i=0; i<=31; i++) {
        moons[i][0] = '\0';
    }
}

static void
SetMoonEntry(int jul, char const *moon)
{
    int phase;
    int y, m, d;
    char msg[32];

    /* Don't bother unless it's utf-8 */
    if (!encoding_is_utf8) {
        return;
    }

    msg[0] = 0;
    if (sscanf(moon, "%d %*d %*d %31[^\x01]", &phase, msg) < 4) {
        if (sscanf(moon, "%d", &phase) != 1) {
            /* Malformed MOON special; ignore */
            fprintf(stderr, "Oops 1\n");
            return;
        }
    }
    if (phase < 0 || phase > 3) {
        /* Bad phase */
        fprintf(stderr, "Oops 2\n");
        return;
    }
    FromJulian(jul, &y, &m, &d);
    if (msg[0]) {
        snprintf(moons[d], sizeof(moons[d]), "%s %s", moonphase_emojis[phase], msg);
    } else {
        snprintf(moons[d], sizeof(moons[d]), "%s", moonphase_emojis[phase]);
    }
}


/***************************************************************/
/*                                                             */
/*  ProduceCalendar                                            */
/*                                                             */
/*  Main loop for generating a calendar.                       */
/*                                                             */
/***************************************************************/
void ProduceCalendar(void)
{
    int y, m, d;

    /* Check if current locale is UTF-8, if we have langinfo.h */
#ifdef HAVE_LANGINFO_H
    char const *encoding = nl_langinfo(CODESET);
    if (!strcasecmp(encoding, "utf-8")) {
        encoding_is_utf8 = 1;
    }

#endif
    if (UseUTF8Chars) {
	linestruct = &UTF8Drawing;
    } else if (UseVTChars) {
        linestruct = &VT100Drawing;
    } else {
        linestruct = &NormalDrawing;
    }
    ShouldCache = 1;

    CalWidth = ComputeCalWidth(CalWidth);

    ColSpaces = (CalWidth - 9) / 7;
    CalWidth = 7*ColSpaces + 8;

    /* Run the file once to get potentially-overridden day names */
    if (CalMonths) {
        FromJulian(JulianToday, &y, &m, &d);
        JulianToday = Julian(y, m, 1);
        GenerateCalEntries(-1);
	DidAMonth = 0;
	if (PsCal == PSCAL_LEVEL3) {
	    printf("[\n");
	}
	while (CalMonths--) {
	    DoCalendarOneMonth();
	    DidAMonth = 1;
	}
	if (PsCal == PSCAL_LEVEL3) {
	    printf("\n]\n");
	}
	return;
    } else {
	if (MondayFirst) JulianToday -= (JulianToday%7);
	else             JulianToday -= ((JulianToday+1)%7);

        GenerateCalEntries(-1);

	if (!DoSimpleCalendar) {
  	    WriteWeekHeaderLine();
	    WriteCalDays();
	    WriteIntermediateCalLine();
	}

	while (CalWeeks--)
	    DoCalendarOneWeek(CalWeeks);
	return;
    }
}

/***************************************************************/
/*                                                             */
/*  DoCalendarOneWeek                                          */
/*                                                             */
/*  Write a calendar for a single week                         */
/*                                                             */
/***************************************************************/
static void DoCalendarOneWeek(int nleft)
{
    int y, m, d, done, i, l, wd;
    char buf[128];
    int LinesWritten = 0;
    int OrigJul = JulianToday;

    InitMoons();
/* Fill in the column entries */
    for (i=0; i<7; i++) {
	GenerateCalEntries(i);
	JulianToday++;
    }

/* Output the entries */

/* If it's "Simple Calendar" format, do it simply... */
    if (DoSimpleCalendar) {
	if (MondayFirst) wd = JulianToday % 7;
	else             wd = (JulianToday + 1) % 7;
	for (i=0; i<7; i++) {
	    WriteSimpleEntries(i, OrigJul+i-wd);
	}
	return;
    }

/* Here come the first few lines... */
    gon();
    DRAW(tb);
    goff();
    for (i=0; i<7; i++) {
	FromJulian(OrigJul+i, &y, &m, &d);
        char const *mon = get_month_name(m);
        if (moons[d]) {
            snprintf(buf, sizeof(buf), "%d %s %s ", d, get_month_abbrev(mon), moons[d]);
        } else {
            snprintf(buf, sizeof(buf), "%d %s ", d, get_month_abbrev(mon));
        }
	if (OrigJul+i == RealToday)
	    PrintLeft(buf, ColSpaces, '*');
	else
	    PrintLeft(buf, ColSpaces, ' ');
	gon();
	DRAW(tb);
	goff();
    }
    putchar('\n');
    for (l=0; l<CalPad; l++) {
	gon();
	DRAW(tb);
	goff();
	for (i=0; i<7; i++) {
	    PrintLeft("", ColSpaces, ' ');
	    gon();
	    DRAW(tb);
	    goff();
	}
	putchar('\n');
    }

/* Write the body lines */
    done = 0;
    while (!done) {
	done = WriteOneCalLine();
	LinesWritten++;
    }

/* Write any blank lines required */
    while (LinesWritten++ < CalLines) {
        gon();
        DRAW(tb);
        goff();
	for (i=0; i<7; i++) {
	    PrintLeft("", ColSpaces, ' ');
	    gon();
	    DRAW(tb);
	    goff();
	}
	putchar('\n');
    }

/* Write the final line */
    if (nleft) {
        WriteIntermediateCalLine();
    } else {
        WriteBottomCalLine();
    }
}

/***************************************************************/
/*                                                             */
/*  DoCalendarOneMonth                                         */
/*                                                             */
/*  Produce a calendar for the current month.                  */
/*                                                             */
/***************************************************************/
static void DoCalendarOneMonth(void)
{
    int y, m, d, mm, yy, i, j;

    InitMoons();

    if (!DoSimpleCalendar) WriteCalHeader();

    DidADay = 0;

    if (PsCal) {
	FromJulian(JulianToday, &y, &m, &d);
	if (PsCal == PSCAL_LEVEL1) {
	    printf("%s\n", PSBEGIN);
	} else if (PsCal == PSCAL_LEVEL2) {
	    printf("%s\n", PSBEGIN2);
	} else {
	    if (DidAMonth) {
		printf(",\n");
	    }
	    printf("{\n");
	}
	if (PsCal < PSCAL_LEVEL3) {
	    printf("%s %d %d %d %d\n",
		   despace(get_month_name(m)), y, DaysInMonth(m, y), (JulianToday+1) % 7,
		   MondayFirst);
            for (i=0; i<7; i++) {
                j=(i+6)%7;
                if (i) {
                    printf(" %s", despace(get_day_name(j)));
                } else {
                    printf("%s", despace(get_day_name(j)));
                }
            }
            printf("\n");
	} else {
	    PrintJSONKeyPairString("monthname", get_month_name(m));
	    PrintJSONKeyPairInt("year", y);
	    PrintJSONKeyPairInt("daysinmonth", DaysInMonth(m, y));
	    PrintJSONKeyPairInt("firstwkday", (JulianToday+1) % 7);
	    PrintJSONKeyPairInt("mondayfirst", MondayFirst);
	    printf("\"daynames\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],",
		   get_day_name(6), get_day_name(0), get_day_name(1), get_day_name(2),
		   get_day_name(3), get_day_name(4), get_day_name(5));
	}
	mm = m-1;
	if (mm<0) {
	    mm = 11; yy = y-1;
	} else yy=y;

	if (PsCal < PSCAL_LEVEL3) {
	    printf("%s %d\n", despace(get_month_name(mm)), DaysInMonth(mm,yy));
	} else {
	    PrintJSONKeyPairString("prevmonthname", get_month_name(mm));
	    PrintJSONKeyPairInt("daysinprevmonth", DaysInMonth(mm, yy));
	    PrintJSONKeyPairInt("prevmonthyear", yy);
	}
	mm = m+1;
	if (mm>11) {
	    mm = 0; yy = y+1;
	} else yy=y;
	if (PsCal < PSCAL_LEVEL3) {
	    printf("%s %d\n", despace(get_month_name(mm)), DaysInMonth(mm,yy));
	} else {
	    PrintJSONKeyPairString("nextmonthname", get_month_name(mm));
	    PrintJSONKeyPairInt("daysinnextmonth", DaysInMonth(mm, yy));
	    PrintJSONKeyPairInt("nextmonthyear", yy);
	    printf("\"entries\":[\n");
	}
    }
    while (WriteCalendarRow()) continue;

    if (PsCal == PSCAL_LEVEL1) {
	printf("%s\n", PSEND);
    } else if (PsCal == PSCAL_LEVEL2) {
	printf("%s\n", PSEND2);
    } else if (PsCal == PSCAL_LEVEL3){
	if (DidADay) {
	    printf("\n");
	}
	printf("]\n}");
    }
    if (!DoSimpleCalendar) WriteCalTrailer();
}

/***************************************************************/
/*                                                             */
/*  WriteCalendarRow                                           */
/*                                                             */
/*  Write one row of the calendar                              */
/*                                                             */
/***************************************************************/
static int WriteCalendarRow(void)
{
    int y, m, d, wd, i, l;
    int done;
    char buf[81];
    int OrigJul = JulianToday;
    int LinesWritten = 0;
    int moreleft;

/* Get the date of the first day */
    FromJulian(JulianToday, &y, &m, &d);
    if (!MondayFirst) wd = (JulianToday + 1) % 7;
    else		     wd = JulianToday % 7;

/* Fill in the column entries */
    for (i=wd; i<7; i++) {
	if (d+i-wd > DaysInMonth(m, y)) break;
	GenerateCalEntries(i);
	JulianToday++;
    }

/* Output the entries */

/* If it's "Simple Calendar" format, do it simply... */
    if (DoSimpleCalendar) {
	for (i=wd; i<7 && d+i-wd<=DaysInMonth(m, y); i++) {
	    WriteSimpleEntries(i, OrigJul+i-wd);
	}
	return (d+7-wd <= DaysInMonth(m, y));
    }


/* Here come the first few lines... */
    gon();
    DRAW(tb);
    goff();
    for (i=0; i<7; i++) {
	if (i < wd || d+i-wd>DaysInMonth(m, y))
	    PrintLeft("", ColSpaces, ' ');
	else {
            if (moons[d+i-wd]) {
                snprintf(buf, sizeof(buf), "%d %s", d+i-wd, moons[d+i-wd]);
            } else {
                snprintf(buf, sizeof(buf), "%d", d+i-wd);
            }
	    if (Julian(y, m, d+i-wd) == RealToday) {
		PrintLeft(buf, ColSpaces-1, '*');
		putchar(' ');
	    } else {
		PrintLeft(buf, ColSpaces, ' ');
	    }
	}
	gon();
	DRAW(tb);
	goff();
    }
    putchar('\n');
    for (l=0; l<CalPad; l++) {
        gon();
	DRAW(tb);
	goff();
	for (i=0; i<7; i++) {
	    PrintLeft("", ColSpaces, ' ');
	    gon();
	    DRAW(tb);
	    goff();
	}
	putchar('\n');
    }

/* Write the body lines */
    done = 0;
    while (!done) {
	done = WriteOneCalLine();
	LinesWritten++;
    }

/* Write any blank lines required */
    while (LinesWritten++ < CalLines) {
        gon();
	DRAW(tb);
	goff();
	for (i=0; i<7; i++) {
	    PrintLeft("", ColSpaces, ' ');
	    gon();
	    DRAW(tb);
	    goff();
	}
	putchar('\n');
    }

    moreleft = (d+7-wd <= DaysInMonth(m, y));
    if (moreleft) {
        WriteIntermediateCalLine();
    } else {
        WriteBottomCalLine();
    }

/* Return non-zero if we have not yet finished */
    return moreleft;
}

/***************************************************************/
/*                                                             */
/*  PrintLeft                                                  */
/*                                                             */
/*  Left-justify a piece of text.                              */
/*                                                             */
/***************************************************************/
static void PrintLeft(char const *s, int width, char pad)
{
#ifndef REM_USE_WCHAR
    int len = strlen(s);
    printf("%s", s);
    while (len++ < width) putchar(pad);
#else
    size_t len = mbstowcs(NULL, s, 0);
    int i;
    wchar_t static_buf[128];
    wchar_t *buf;
    wchar_t *ws;
    int display_len;

    if (!len) {
	for (i=0; i<width; i++) {
	    fputc(pad, stdout);
	}
	return;
    }
    if (len + 1 <= 128) {
	buf = static_buf;
    } else {
	buf = calloc(len+1, sizeof(wchar_t));
	if (!buf) {
	    /* Uh-oh... cannot recover */
	    fprintf(stderr, "%s\n", ErrMsg[E_NO_MEM]);
	    exit(1);
	}
    }
    (void) mbstowcs(buf, s, len+1);
    display_len = wcswidth(buf, len+1);

    ws = buf;
    for (i=0; i<width;) {
	if (*ws) {
            PutWideChar(*ws++);
            i+= wcwidth(*ws);
        } else {
            break;
        }
    }
    /* Mop up any potential combining characters */
    while (*ws && wcwidth(*ws) == 0) {
        PutWideChar(*ws++);
    }

    /* Possibly send lrm control sequence */
    send_lrm();

    for (i=display_len; i<width; i++) fputc(pad, stdout);
    if (buf != static_buf) free(buf);
#endif

}

/***************************************************************/
/*                                                             */
/*  PrintCentered                                              */
/*                                                             */
/*  Center a piece of text                                      */
/*                                                             */
/***************************************************************/
static void PrintCentered(char const *s, int width, char *pad)
{
#ifndef REM_USE_WCHAR
    int len = strlen(s);
    int d = (width - len) / 2;
    int i;

    for (i=0; i<d; i++) fputs(pad, stdout);
    for (i=0; i<width; i++) {
	if (*s) {
            if (isspace(*s)) {
                putchar(' ');
                s++;
            } else {
                putchar(*s++);
            }
        } else {
            break;
        }
    }
    for (i=d+len; i<width; i++) fputs(pad, stdout);
#else
    size_t len = mbstowcs(NULL, s, 0);
    int display_len;
    int i;
    wchar_t static_buf[128];
    wchar_t *buf;
    wchar_t *ws;
    int d;

    if (!len) {
	for (i=0; i<width; i++) {
	    fputs(pad, stdout);
	}
	return;
    }
    if (len + 1 <= 128) {
	buf = static_buf;
    } else {
	buf = calloc(len+1, sizeof(wchar_t));
	if (!buf) {
	    /* Uh-oh... cannot recover */
	    fprintf(stderr, "%s\n", ErrMsg[E_NO_MEM]);
	    exit(1);
	}
    }
    (void) mbstowcs(buf, s, len+1);
    display_len = wcswidth(buf, len+1);
    d = (width - display_len) / 2;
    if (d < 0) d = 0;
    ws = buf;
    for (i=0; i<d; i++) fputs(pad, stdout);
    for (i=0; i<width; i++) {
	if (*ws) {
            PutWideChar(*ws++);
            if (wcwidth(*ws) == 0) {
                /* Don't count this character... it's zero-width */
                i--;
            }
        } else {
            break;
        }
    }
    /* Mop up any potential combining characters */
    while (*ws && wcwidth(*ws) == 0) {
        PutWideChar(*ws++);
    }
    /* Possibly send lrm control sequence */
    send_lrm();

    for (i=d+display_len; i<width; i++) fputs(pad, stdout);
    if (buf != static_buf) free(buf);
#endif
}

/***************************************************************/
/*                                                             */
/*  WriteOneCalLine                                            */
/*                                                             */
/*  Write a single line.                                       */
/*                                                             */
/***************************************************************/
static int WriteOneCalLine(void)
{
    int done = 1, i;

    gon();
    DRAW(tb);
    goff();
    for (i=0; i<7; i++) {
	if (CalColumn[i]) {
	    if (WriteOneColLine(i)) done = 0;
	} else {
	    PrintCentered("", ColSpaces, " ");
	}
	gon();
	DRAW(tb);
	goff();
    }
    putchar('\n');

    return done;
}

/***************************************************************/
/*                                                             */
/*  WriteOneColLine                                            */
/*                                                             */
/*  Write a single line for a specified column.  Return 1 if   */
/*  the column still has entries; 0 otherwise.                 */
/*                                                             */
/***************************************************************/
static int WriteOneColLine(int col)
{
    CalEntry *e = CalColumn[col];
    char const *s;
    char const *space;

#ifdef REM_USE_WCHAR
    wchar_t const *ws;
    wchar_t const *wspace;
    int width;
#endif

    int numwritten = 0;

    /* Print as many characters as possible within the column */
#ifdef REM_USE_WCHAR
    if (e->wc_text) {
	wspace = NULL;
	ws = e->wc_pos;

	/* If we're at the end, and there's another entry, do a blank
	   line and move to next entry. */
	if (!*ws && e->next) {
	    PrintLeft("", ColSpaces, ' ');
	    CalColumn[col] = e->next;
	    free(e->text);
	    free(e->raw_text);
	    free(e->filename);
	    if (e->wc_text) free(e->wc_text);
	    free(e);
	    return 1;
	}

	/* Find the last space char within the column. */
        width = 0;
	while (width <= ColSpaces) {
	    if (!*ws) {
                wspace = ws;
                break;
            }
	    if (iswspace(*ws)) {
                wspace = ws;
                width++;
            } else {
                if (wcwidth(*ws)) {
                    width += wcwidth(*ws);
                    if (width > ColSpaces) {
                        break;
                    }
                }
            }
	    ws++;
	}

	/* Colorize reminder if necessary */
	if (UseVTColors && e->is_color) {
	    ColorizeEntry(e);
	}

	/* If we couldn't find a space char, print what we have. */
	if (!wspace) {
	    for (ws = e->wc_pos; numwritten < ColSpaces; ws++) {
		if (!*ws) break;
                if (iswspace(*ws)) {
                    putchar(' ');
                    numwritten++;
                } else {
                    if (wcwidth(*ws) > 0) {
                        if (numwritten + wcwidth(*ws) > ColSpaces) {
                            break;
                        }
                        numwritten += wcwidth(*ws);
                    }
                    PutWideChar(*ws);
                }
	    }
	    e->wc_pos = ws;
	} else {
	    /* We found a space - print everything before it. */
	    for (ws = e->wc_pos; ws<wspace; ws++) {
		if (!*ws) break;
                if (iswspace(*ws)) {
                    putchar(' ');
                    numwritten++;
                } else {
                    if (wcwidth(*ws) > 0) {
                        numwritten += wcwidth(*ws);
                    }
                    PutWideChar(*ws);
                }
	    }
	}

	/* Decolorize reminder if necessary */
	if (UseVTColors && e->is_color) {
	    printf("%s", Decolorize(e->r, e->g, e->b));
	}

        /* Possibly send lrm control sequence */
        send_lrm();

	/* Flesh out the rest of the column */
	while(numwritten++ < ColSpaces) putchar(' ');

	/* Skip any spaces before next word */
	while (iswspace(*ws)) ws++;

	/* If done, free memory if no next entry. */
	if (!*ws && !e->next) {
	    CalColumn[col] = e->next;
	    free(e->text);
	    free(e->raw_text);
	    free(e->filename);
	    if (e->wc_text) free(e->wc_text);
	    free(e);
	} else {
	    e->wc_pos = ws;
	}
	if (CalColumn[col]) return 1; else return 0;
    } else {
#endif
	space = NULL;
	s = e->pos;

	/* If we're at the end, and there's another entry, do a blank
	   line and move to next entry. */
	if (!*s && e->next) {
	    PrintLeft("", ColSpaces, ' ');
	    CalColumn[col] = e->next;
	    free(e->text);
	    free(e->filename);
#ifdef REM_USE_WCHAR
	    if (e->wc_text) free(e->wc_text);
#endif
	    free(e->raw_text);
	    free(e);
	    return 1;
	}

	/* Find the last space char within the column. */
	while (s - e->pos <= ColSpaces) {
	    if (!*s) {space = s; break;}
	    if (isspace(*s)) space = s;
	    s++;
	}

	/* Colorize reminder if necessary */
	if (UseVTColors && e->is_color) {
	    ColorizeEntry(e);
	}

	/* If we couldn't find a space char, print what we have. */
	if (!space) {
	    for (s = e->pos; s - e->pos < ColSpaces; s++) {
		if (!*s) break;
		numwritten++;
                if (isspace(*s)) {
                    putchar(' ');
                } else {
                    putchar(*s);
                }
	    }
	    e->pos = s;
	} else {
	    /* We found a space - print everything before it. */
	    for (s = e->pos; s<space; s++) {
		if (!*s) break;
		numwritten++;
                if (isspace(*s)) {
                    putchar(' ');
                } else {
                    putchar(*s);
                }
	    }
	}

	/* Decolorize reminder if necessary */
	if (UseVTColors && e->is_color) {
	    printf("%s", Decolorize(e->r, e->g, e->b));
	}

	/* Flesh out the rest of the column */
	while(numwritten++ < ColSpaces) putchar(' ');

	/* Skip any spaces before next word */
	while (isspace(*s)) s++;

	/* If done, free memory if no next entry. */
	if (!*s && !e->next) {
	    CalColumn[col] = e->next;
	    free(e->text);
	    free(e->filename);
#ifdef REM_USE_WCHAR
	    if (e->wc_text) free(e->wc_text);
#endif
	    free(e->raw_text);
	    free(e);
	} else {
	    e->pos = s;
	}
	if (CalColumn[col]) return 1; else return 0;
#ifdef REM_USE_WCHAR
    }
#endif
}

/***************************************************************/
/*                                                             */
/*  GenerateCalEntries                                         */
/*                                                             */
/*  Generate the calendar entries for the ith column           */
/*                                                             */
/***************************************************************/
static void GenerateCalEntries(int col)
{
    int r;
    Token tok;
    char const *s;
    Parser p;

/* Do some initialization first... */
    PerIterationInit();

    r=IncludeFile(InitialFile);
    if (r) {
	fprintf(ErrFp, "%s %s: %s\n", ErrMsg[E_ERR_READING], InitialFile, ErrMsg[r]);
	exit(1);
    }

    while(1) {
	r = ReadLine();
	if (r == E_EOF) return;
	if (r) {
	    Eprint("%s: %s", ErrMsg[E_ERR_READING], ErrMsg[r]);
	    exit(1);
	}
	s = FindInitialToken(&tok, CurLine);

	/* Should we ignore it? */
	if (NumIfs &&
	    tok.type != T_If &&
	    tok.type != T_Else &&
	    tok.type != T_EndIf &&
	    tok.type != T_IfTrig &&
	    ShouldIgnoreLine())
	{
	    /* DO NOTHING */
	}
	else {
	    /* Create a parser to parse the line */
	    CreateParser(s, &p);

	    switch(tok.type) {

	    case T_Empty:
	    case T_Comment:
		break;

	    case T_ErrMsg:  r=DoErrMsg(&p);  break;
	    case T_Rem:     r=DoCalRem(&p, col); break;
	    case T_If:      r=DoIf(&p);      break;
	    case T_IfTrig:  r=DoIfTrig(&p);  break;
	    case T_Else:    r=DoElse(&p);    break;
	    case T_EndIf:   r=DoEndif(&p);   break;

	    case T_Include:
	    case T_IncludeR: r=DoInclude(&p, tok.type); break;

	    case T_IncludeCmd: r=DoIncludeCmd(&p); break;
	    case T_Exit:    DoExit(&p);	     break;
	    case T_Set:     r=DoSet(&p);     break;
	    case T_Fset:    r=DoFset(&p);    break;
	    case T_UnSet:   r=DoUnset(&p);   break;
	    case T_Clr:     r=DoClear(&p);   break;
	    case T_Flush:   r=DoFlush(&p);   break;
	    case T_Debug:   break;  /* IGNORE DEBUG CMD */
	    case T_Dumpvars: break; /* IGNORE DUMPVARS CMD */
	    case T_Banner:  break;  /* IGNORE BANNER CMD */
	    case T_Omit:    r=DoOmit(&p);
		if (r == E_PARSE_AS_REM) {
		    DestroyParser(&p);
		    CreateParser(s, &p);
		    r=DoCalRem(&p, col);
		}
		break;
	    case T_Pop:     r=PopOmitContext(&p);     break;
	    case T_Push:    r=PushOmitContext(&p);    break;
	    case T_Preserve: r=DoPreserve(&p);        break;
	    case T_RemType: if (tok.val == RUN_TYPE) {
		r=DoRun(&p);
		break;
	    } else {
		CreateParser(CurLine, &p);
		r=DoCalRem(&p, col);
		break;
	    }

	    /* If we don't recognize the command, do a REM by default */
	    /* Note:  Since the parser hasn't been used yet, we don't */
	    /* need to destroy it here. */

	    default:        CreateParser(CurLine, &p);
		r=DoCalRem(&p, col);
		break;
	    }
	    if (r && (!Hush || r != E_RUN_DISABLED)) Eprint("%s", ErrMsg[r]);

	    /* Destroy the parser - free up resources it may be tying up */
	    DestroyParser(&p);
	}
    }
}


/***************************************************************/
/*                                                             */
/*  WriteCalHeader                                             */
/*                                                             */
/***************************************************************/
static void WriteCalHeader(void)
{
    char buf[80];
    int y, m, d;

    FromJulian(JulianToday, &y, &m, &d);
    sprintf(buf, "%s %d", get_month_name(m), y);

    WriteTopCalLine();

    gon();
    DRAW(tb);
    goff();
    PrintCentered(buf, CalWidth-2, " ");
    gon();
    DRAW(tb);
    goff();
    putchar('\n');

    WritePostHeaderLine();
    WriteCalDays();
    WriteIntermediateCalLine();
}

/***************************************************************/
/*                                                             */
/*  WriteCalTrailer                                            */
/*                                                             */
/***************************************************************/
static void WriteCalTrailer(void)
{
    putchar('\f');
}

/***************************************************************/
/*                                                             */
/*  DoCalRem                                                   */
/*                                                             */
/*  Do the REM command in the context of a calendar.           */
/*                                                             */
/***************************************************************/
static int DoCalRem(ParsePtr p, int col)
{
    size_t oldLen;
    Trigger trig;
    TimeTrig tim;
    Value v;
    int r, err;
    int jul;
    CalEntry *CurCol;
    CalEntry *e;
    char const *s, *s2;
    DynamicBuffer buf, obuf, pre_buf, raw_buf;
    Token tok;
    int nonconst_expr = 0;

    int is_color, col_r, col_g, col_b;

    if (col >= 0) {
        CurCol = CalColumn[col];
    } else {
        CurCol = NULL;
    }
    is_color = 0;
    DBufInit(&buf);
    DBufInit(&pre_buf);
    DBufInit(&raw_buf);

    /* Parse the trigger date and time */
    if ( (r=ParseRem(p, &trig, &tim, 1)) ) {
	FreeTrig(&trig);
	return r;
    }

    if (trig.typ == MSG_TYPE ||
	trig.typ == CAL_TYPE ||
	trig.typ == MSF_TYPE) {
	is_color = (
	    DefaultColorR != -1
	    && DefaultColorG != -1
	    && DefaultColorB != -1);
	if (is_color) {
	    col_r = DefaultColorR;
	    col_g = DefaultColorG;
	    col_b = DefaultColorB;
	}
    }

    if (trig.typ == NO_TYPE) {
	FreeTrig(&trig);
	return E_EOLN;
    }
    if (trig.typ == SAT_TYPE) {
	r=DoSatRemind(&trig, &tim, p);
	if (r) {
            if (r == E_CANT_TRIG && trig.maybe_uncomputable) {
                r = OK;
            }
	    FreeTrig(&trig);
	    if (r == E_EXPIRED) return OK;
	    return r;
	}
	if (!LastTrigValid) {
	    FreeTrig(&trig);
	    return OK;
	}
	r=ParseToken(p, &buf);
	if (r) {
	    FreeTrig(&trig);
	    return r;
	}
	FindToken(DBufValue(&buf), &tok);
	DBufFree(&buf);
	if (tok.type == T_Empty || tok.type == T_Comment) {
	    FreeTrig(&trig);
	    return OK;
	}
	if (tok.type != T_RemType || tok.val == SAT_TYPE) {
	    FreeTrig(&trig);
	    return E_PARSE_ERR;
	}
	if (tok.val == PASSTHRU_TYPE) {
	    r=ParseToken(p, &buf);
	    if (r) return r;
	    if (!DBufLen(&buf)) {
		DBufFree(&buf);
		FreeTrig(&trig);
		return E_EOLN;
	    }
	    StrnCpy(trig.passthru, DBufValue(&buf), PASSTHRU_LEN);
	    DBufFree(&buf);
	}
	trig.typ = tok.val;
	jul = LastTriggerDate;
	if (!LastTrigValid) {
	    FreeTrig(&trig);
	    return OK;
	}
    } else {
	/* Calculate the trigger date */
	jul = ComputeTrigger(trig.scanfrom, &trig, &tim, &r, 1);
	if (r) {
            if (r == E_CANT_TRIG && trig.maybe_uncomputable) {
                r = OK;
            }
	    FreeTrig(&trig);
	    return r;
	}
    }

    /* Add to global OMITs if so indicated */
    if (trig.addomit) {
        r = AddGlobalOmit(jul);
        if (r) {
	    FreeTrig(&trig);
            return r;
        }
    }

    /* If we're not actually generating any calendar entries, we're done */
    if (col < 0) {
	FreeTrig(&trig);
        return OK;
    }

    /* Don't include timed reminders in calendar if -a option supplied. */
    if (DontIssueAts && tim.ttime != NO_TIME) {
	FreeTrig(&trig);
	return OK;
    }

    /* Save nonconst_expr flag */
    nonconst_expr = p->nonconst_expr;
    /* Convert PS and PSF to PASSTHRU */
    if (trig.typ == PS_TYPE) {
	strcpy(trig.passthru, "PostScript");
	trig.typ = PASSTHRU_TYPE;
    } else if (trig.typ == PSF_TYPE) {
	strcpy(trig.passthru, "PSFile");
	trig.typ = PASSTHRU_TYPE;
    }

    /* If it's a plain reminder but we have a default color, add the
       three colors to the prebuf and change passthru to "COLOR" */
    if (trig.typ == MSG_TYPE ||
	trig.typ == CAL_TYPE ||
	trig.typ == MSF_TYPE) {
	if (PsCal && is_color) {
	    char cbuf[24];
	    sprintf(cbuf, "%d %d %d ", col_r, col_g, col_b);
	    DBufPuts(&pre_buf, cbuf);
	    strcpy(trig.passthru, "COLOR");
	    /* Don't change trig.typ or next if() will trigger! */
	}
    }
    if (trig.typ == PASSTHRU_TYPE) {
	if (!PsCal && StrCmpi(trig.passthru, "COLOR") && StrCmpi(trig.passthru, "COLOUR") && StrCmpi(trig.passthru, "MOON")) {
	    FreeTrig(&trig);
	    return OK;
	}
        if (!PsCal && !StrCmpi(trig.passthru, "MOON")) {
            if (jul == JulianToday) {
                DBufInit(&obuf);
                r = DoSubst(p, &obuf, &trig, &tim, jul, CAL_MODE);
                if (r) {
                    DBufFree(&obuf);
                    FreeTrig(&trig);
                    return r;
                }
                SetMoonEntry(jul, DBufValue(&obuf));
                DBufFree(&obuf);
            }
        }
	if (!StrCmpi(trig.passthru, "COLOR") ||
	    !StrCmpi(trig.passthru, "COLOUR")) {
	    is_color = 1;
	    /* Strip off the three color numbers */
	    DBufFree(&buf);
	    r=ParseToken(p, &buf);
	    DBufPuts(&pre_buf, DBufValue(&buf));
	    DBufPutc(&pre_buf, ' ');
	    DBufFree(&buf);
	    if (r) {
		FreeTrig(&trig);
		return r;
	    }
	    r=ParseToken(p, &buf);
	    DBufPuts(&pre_buf, DBufValue(&buf));
	    DBufPutc(&pre_buf, ' ');
	    DBufFree(&buf);
	    if (r) {
		FreeTrig(&trig);
		return r;
	    }
	    r=ParseToken(p, &buf);
	    DBufPuts(&pre_buf, DBufValue(&buf));
	    DBufPutc(&pre_buf, ' ');
	    DBufFree(&buf);
	    if (r) {
		FreeTrig(&trig);
		return r;
	    }
	    (void) sscanf(DBufValue(&pre_buf), "%d %d %d",
			  &col_r, &col_g, &col_b);
	    if (col_r < 0) col_r = 0;
	    else if (col_r > 255) col_r = 255;
	    if (col_g < 0) col_g = 0;
	    else if (col_g > 255) col_g = 255;
	    if (col_b < 0) col_b = 0;
	    else if (col_b > 255) col_b = 255;
	    if (!PsCal && !DoSimpleCalendar) {
		DBufFree(&pre_buf);
	    }
	}
    }

    /* If trigger date == today, add it to the current entry */
    DBufInit(&obuf);
    if ((jul == JulianToday) ||
	(DoSimpleCalDelta &&
	 ShouldTriggerReminder(&trig, &tim, jul, &err))) {
	NumTriggered++;

	/* The parse_ptr should not be nested, but just in case... */
	if (!p->isnested) {
	    if (DBufPuts(&raw_buf, p->pos) != OK) {
		DBufFree(&obuf);
		DBufFree(&pre_buf);
		FreeTrig(&trig);
		return E_NO_MEM;
	    }
	}
	if (DoSimpleCalendar || tim.ttime != NO_TIME) {
	    /* Suppress time if it's not today or if it's a non-COLOR special */
	    if (jul != JulianToday ||
		(trig.typ == PASSTHRU_TYPE &&
		 StrCmpi(trig.passthru, "COLOUR") &&
		 StrCmpi(trig.passthru, "COLOR"))) {
		if (DBufPuts(&obuf, SimpleTime(NO_TIME)) != OK) {
		    DBufFree(&obuf);
		    DBufFree(&raw_buf);
		    DBufFree(&pre_buf);
		    FreeTrig(&trig);
		    return E_NO_MEM;
		}
	    } else {
		if (DBufPuts(&obuf, CalendarTime(tim.ttime, tim.duration)) != OK) {
		    DBufFree(&raw_buf);
		    DBufFree(&obuf);
		    DBufFree(&pre_buf);
		    FreeTrig(&trig);
		    return E_NO_MEM;
		}
	    }
	}
	if (trig.typ != PASSTHRU_TYPE &&
	    UserFuncExists("calprefix")==1) {
	    char evalBuf[64];
	    sprintf(evalBuf, "calprefix(%d)", trig.priority);
	    s2 = evalBuf;
	    r = EvalExpr(&s2, &v, NULL);
	    if (!r) {
		if (!DoCoerce(STR_TYPE, &v)) {
		    if (DBufPuts(&obuf, v.v.str) != OK) {
			DestroyValue(v);
			DBufFree(&raw_buf);
			DBufFree(&obuf);
			DBufFree(&pre_buf);
			FreeTrig(&trig);
			return E_NO_MEM;
		    }
		}
		DestroyValue(v);
	    }
	}
	oldLen = DBufLen(&obuf);

	/* In -sa mode, run in ADVANCE mode if we're triggering
	 * before the actual date */
	if (jul != JulianToday) {
	    r = DoSubst(p, &obuf, &trig, &tim, jul, ADVANCE_MODE);
	} else {
	    r = DoSubst(p, &obuf, &trig, &tim, jul, CAL_MODE);
	}
	if (r) {
	    DBufFree(&pre_buf);
	    DBufFree(&obuf);
	    DBufFree(&raw_buf);
	    FreeTrig(&trig);
	    return r;
	}
	if (DBufLen(&obuf) <= oldLen) {
	    DBufFree(&obuf);
	    DBufFree(&pre_buf);
	    DBufFree(&raw_buf);
	    FreeTrig(&trig);
	    return OK;
	}
	if (trig.typ != PASSTHRU_TYPE &&
	    UserFuncExists("calsuffix")==1) {
	    char evalBuf[64];
	    sprintf(evalBuf, "calsuffix(%d)", trig.priority);
	    s2 = evalBuf;
	    r = EvalExpr(&s2, &v, NULL);
	    if (!r) {
		if (!DoCoerce(STR_TYPE, &v)) {
		    if (DBufPuts(&obuf, v.v.str) != OK) {
			DestroyValue(v);
			DBufFree(&raw_buf);
			DBufFree(&obuf);
			DBufFree(&pre_buf);
			FreeTrig(&trig);
			return E_NO_MEM;
		    }
		}
		DestroyValue(v);
	    }
	}
	s = DBufValue(&obuf);
	if (!DoSimpleCalendar) while (isempty(*s)) s++;
	DBufPuts(&pre_buf, s);
	s = DBufValue(&pre_buf);
	e = NEW(CalEntry);
	if (!e) {
	    DBufFree(&obuf);
	    DBufFree(&raw_buf);
	    DBufFree(&pre_buf);
	    FreeTrig(&trig);
	    return E_NO_MEM;
	}
	e->nonconst_expr = nonconst_expr;
	e->if_depth = NumIfs;
	e->trig = trig;
	e->tt = tim;
#ifdef REM_USE_WCHAR
	e->wc_pos = NULL;
	e->wc_text = NULL;
#endif
	e->is_color = is_color;
	e->r = col_r;
	e->g = col_g;
	e->b = col_b;
	e->text = StrDup(s);
	e->raw_text = StrDup(DBufValue(&raw_buf));
	DBufFree(&raw_buf);
	DBufFree(&obuf);
	DBufFree(&pre_buf);
	if (!e->text || !e->raw_text) {
	    if (e->text) free(e->text);
	    if (e->raw_text) free(e->raw_text);
	    free(e);
	    FreeTrig(&trig);
	    return E_NO_MEM;
	}
	make_wchar_versions(e);
	DBufInit(&(e->tags));
	DBufPuts(&(e->tags), DBufValue(&(trig.tags)));
	if (SynthesizeTags) {
	    AppendTag(&(e->tags), SynthesizeTag());
	}

	/* Don't need tags any more */
	FreeTrig(&trig);
	e->duration = tim.duration;
	e->priority = trig.priority;
	e->filename = StrDup(FileName);
	if(!e->filename) {
	    if (e->text) free(e->text);
	    if (e->raw_text) free(e->raw_text);
#ifdef REM_USE_WCHAR
            if (e->wc_text) free(e->wc_text);
#endif
	    free(e);
	    return E_NO_MEM;
	}
	e->lineno = LineNo;

	if (trig.typ == PASSTHRU_TYPE || is_color) {
	    StrnCpy(e->passthru, trig.passthru, PASSTHRU_LEN);
	} else {
	    e->passthru[0] = 0;
	}
	e->pos = e->text;
	if (jul == JulianToday) {
	    e->time = tim.ttime;
	} else {
	    e->time = NO_TIME;
	}
	e->next = CurCol;
	CalColumn[col] = e;
	SortCol(&CalColumn[col]);
    } else {
        /* Parse the rest of the line to catch expression-pasting errors */
        while (ParseChar(p, &r, 0)) {
            if (r != 0) {
                return r;
            }
        }
    }
    return OK;
}

static void WriteSimpleEntryProtocol1(CalEntry *e)
{
	if (e->passthru[0]) {
	    printf(" %s", e->passthru);
	} else {
	    printf(" *");
	}
	if (*DBufValue(&(e->tags))) {
	    printf(" %s ", DBufValue(&(e->tags)));
	} else {
	    printf(" * ");
	}
	if (e->duration != NO_TIME) {
	    printf("%d ", e->duration);
	} else {
	    printf("* ");
	}
	if (e->time != NO_TIME) {
	    printf("%d ", e->time);
	} else {
	    printf("* ");
	}
	printf("%s\n", e->text);
}


static void WriteSimpleEntryProtocol2(CalEntry *e, int today)
{
    int done = 0;
    char const *s;
    if (DoPrefixLineNo) {
	PrintJSONKeyPairString("filename", e->filename);
	PrintJSONKeyPairInt("lineno", e->lineno);
    }
    PrintJSONKeyPairString("passthru", e->passthru);
    PrintJSONKeyPairString("tags", DBufValue(&(e->tags)));
    if (e->duration != NO_TIME) {
	PrintJSONKeyPairInt("duration", e->duration);
    }
    if (e->time != NO_TIME) {
	PrintJSONKeyPairInt("time", e->time);
	if (e->tt.delta) {
	    PrintJSONKeyPairInt("tdelta", e->tt.delta);
	}
	if (e->tt.rep) {
	    PrintJSONKeyPairInt("trep", e->tt.rep);
	}
    }
    if (e->trig.eventduration != NO_TIME) {
	PrintJSONKeyPairInt("eventduration", e->trig.eventduration);
    }
    /* wd is an array of days from 0=monday to 6=sunday.
       We convert to array of strings */
    if (e->trig.wd != NO_WD) {
	printf("\"wd\":[");
	done = 0;
	int i;
	for (i=0; i<7; i++) {
	    if (e->trig.wd & (1 << i)) {
		if (done) {
		    printf(",");
		}
		done = 1;
		printf("\"%s\"", EnglishDayName[i]);
	    }
	}
	printf("],");
    }
    if (e->trig.d != NO_DAY) {
	PrintJSONKeyPairInt("d", e->trig.d);
    }
    if (e->trig.m != NO_MON) {
	PrintJSONKeyPairInt("m", e->trig.m+1);
    }
    if (e->trig.y != NO_YR) {
	PrintJSONKeyPairInt("y", e->trig.y);
    }
    PrintJSONKeyPairDateTime("eventstart", e->trig.eventstart);
    if (e->trig.back) {
	PrintJSONKeyPairInt("back", e->trig.back);
    }
    if (e->trig.delta) {
	PrintJSONKeyPairInt("delta", e->trig.delta);
    }
    if (e->trig.rep) {
	PrintJSONKeyPairInt("rep", e->trig.rep);
    }
    if (e->nonconst_expr) {
	PrintJSONKeyPairInt("nonconst_expr", e->nonconst_expr);
    }
    if (e->if_depth) {
	PrintJSONKeyPairInt("if_depth", e->if_depth);
    }
    switch(e->trig.skip) {
    case SKIP_SKIP:
	PrintJSONKeyPairString("skip", "SKIP");
	break;
    case BEFORE_SKIP:
	PrintJSONKeyPairString("skip", "BEFORE");
	break;
    case AFTER_SKIP:
	PrintJSONKeyPairString("skip", "AFTER");
	break;
    }
    /* Local omit is an array of days from 0=monday to 6=sunday.
       We convert to array of strings */
    if (e->trig.localomit != NO_WD) {
	printf("\"localomit\":[");
	done = 0;
	int i;
	for (i=0; i<7; i++) {
	    if (e->trig.localomit & (1 << i)) {
		if (done) {
		    printf(",");
		}
		done = 1;
		printf("\"%s\"", EnglishDayName[i]);
	    }
	}
	printf("],");
    }
    PrintJSONKeyPairDate("until", e->trig.until);
    if (e->trig.once != NO_ONCE) {
	PrintJSONKeyPairInt("once", e->trig.once);
    }
    if (e->trig.scanfrom != today) {
	PrintJSONKeyPairDate("scanfrom", e->trig.scanfrom);
    }
    PrintJSONKeyPairDate("from", e->trig.from);
    PrintJSONKeyPairInt("priority", e->trig.priority);

    if (e->is_color) {
	PrintJSONKeyPairInt("r", e->r);
	PrintJSONKeyPairInt("g", e->g);
	PrintJSONKeyPairInt("b", e->b);
    } else if (!StrCmpi(e->passthru, "SHADE")) {
	int r, g, b, n;
	n = sscanf(e->text, "%d %d %d", &r, &g, &b);
	if (n < 3) {
	    g = r;
	    b = r;
	}
	if (r < 0) r = 0;
	else if (r > 255) r = 255;
	if (g < 0) g = 0;
	else if (g > 255) g = 255;
	if (b < 0) b = 0;
	else if (b > 255) b = 255;
	PrintJSONKeyPairInt("r", r);
	PrintJSONKeyPairInt("g", g);
	PrintJSONKeyPairInt("b", b);
    }
    /* Only print rawbody if it differs from body */
    if (strcmp(e->raw_text, e->text)) {
	PrintJSONKeyPairString("rawbody", e->raw_text);
    }

    /* Figure out calendar_body and plain_body */
    if (DontSuppressQuoteMarkers) {
        s = strstr(e->text, "%\"");
        if (s) {
            s += 2;
            printf("\"calendar_body\":\"");
            while (*s) {
                if (*s == '%' && *(s+1) == '"') {
                    break;
                }
                PrintJSONChar(*s);
                s++;
            }
            printf("\",");
        }
    }
    s = strstr(e->text, "%\"");
    if (s || e->is_color) {
        printf("\"plain_body\":\"");
        s = e->text;
        if (e->is_color) {
            while(*s && !isspace(*s)) s++;
            while(*s && isspace(*s)) s++;
            while(*s && !isspace(*s)) s++;
            while(*s && isspace(*s)) s++;
            while(*s && !isspace(*s)) s++;
            while(*s && isspace(*s)) s++;
        }
        while(*s) {
            if (*s == '%' && *(s+1) == '"') {
                s += 2;
                continue;
            }
            PrintJSONChar(*s);
            s++;
        }
        printf("\",");
    }
    printf("\"body\":\"");
    PrintJSONString(e->text);
    printf("\"");
}

/***************************************************************/
/*                                                             */
/*  WriteSimpleEntries                                         */
/*                                                             */
/*  Write entries in 'simple calendar' format.                 */
/*                                                             */
/***************************************************************/
static void WriteSimpleEntries(int col, int jul)
{
    CalEntry *e = CalColumn[col];
    CalEntry *n;
    int y, m, d;

    FromJulian(jul, &y, &m, &d);
    while(e) {
	if (DoPrefixLineNo) {
	    if (PsCal != PSCAL_LEVEL2 && PsCal != PSCAL_LEVEL3) {
		printf("# fileinfo %d %s\n", e->lineno, e->filename);
	    }
	}
	if (PsCal >= PSCAL_LEVEL2) {
	    if (PsCal == PSCAL_LEVEL3) {
		if (DidADay) {
		    printf(",\n");
		}
	    }
	    DidADay = 1;
	    printf("{\"date\":\"%04d-%02d-%02d\",", y, m+1, d);
	    WriteSimpleEntryProtocol2(e, jul);
	    printf("}");
	    if (PsCal != PSCAL_LEVEL3) {
		printf("\n");
	    }
	} else {
	    printf("%04d/%02d/%02d", y, m+1, d);
	    WriteSimpleEntryProtocol1(e);
	}

	free(e->text);
	free(e->raw_text);
	free(e->filename);
#ifdef REM_USE_WCHAR
	if (e->wc_text) free(e->wc_text);
#endif
	n = e->next;
	free(e);
	e = n;
    }
    CalColumn[col] = NULL;
}

/***************************************************************/
/*                                                             */
/*  Various functions for writing different types of lines.    */
/*                                                             */
/***************************************************************/
static void WriteTopCalLine(void)
{
    gon();
    DRAW(br);
    PrintCentered("", CalWidth-2, linestruct->lr);
    DRAW(bl);
    goff();
    putchar('\n');
}

static void WriteBottomCalLine(void)
{
    int i;
    gon();
    DRAW(tr);
    for (i=0; i<7; i++) {
	PrintCentered("", ColSpaces, linestruct->lr);
	if (i != 6) {
	    DRAW(tlr);
	} else {
	    DRAW(tl);
	}
    }
    goff();
    putchar('\n');
}

static void WritePostHeaderLine(void)
{
    int i;
    gon();
    DRAW(tbr);
    for (i=0; i<7; i++) {
	PrintCentered("", ColSpaces, linestruct->lr);
	if (i != 6) {
	    DRAW(blr);
	} else {
	    DRAW(tbl);
	}
    }
    goff();
    putchar('\n');
}

static void WriteWeekHeaderLine(void)
{
    int i;
    gon();
    DRAW(br);
    for (i=0; i<7; i++) {
	PrintCentered("", ColSpaces, linestruct->lr);
	if (i != 6) {
	    DRAW(blr);
	} else {
	    DRAW(bl);
	}
    }
    goff();
    putchar('\n');
}

static void WriteIntermediateCalLine(void)
{
    int i;

    gon();
    DRAW(tbr);
    for (i=0; i<7; i++) {
	PrintCentered("", ColSpaces, linestruct->lr);
	if (i != 6) {
	    DRAW(tblr);
	} else {
	    DRAW(tbl);
	}
    }
    goff();
    putchar('\n');
}

static void WriteCalDays(void)
{
    int i;
    gon();
    DRAW(tb);
    goff();
    for (i=0; i<7; i++) {
	if (!MondayFirst)
	    PrintCentered(get_day_name((i+6)%7), ColSpaces, " ");
	else
	    PrintCentered(get_day_name(i%7), ColSpaces, " ");
	gon();
	DRAW(tb);
	goff();
    }
    putchar('\n');
}

/***************************************************************/
/*                                                             */
/*  CalendarTime                                               */
/*                                                             */
/*  Format the time according to simple time format.           */
/*  Answer is returned in a static buffer.                     */
/*  A trailing space is always added.                          */
/*  This takes into account duration                           */
/*                                                             */
/***************************************************************/
char const *
CalendarTime(int tim, int duration)
{
    static char buf[128];
    int h, min, hh;
    int h2, min2, hh2, newtim, days;
    char const *ampm1;
    char const *ampm2;
    char daybuf[64];

    buf[0] = 0;

    if (duration == NO_TIME) {
	/* No duration... just call into SimpleTime */
	return SimpleTime(tim);
    }
    if (tim == NO_TIME) {
	/* No time... nothing to return */
	return buf;
    }
    h = tim/60;
    min = tim % 60;
    if (h == 0)      hh=12;
    else if (h > 12) hh=h-12;
    else             hh = h;

    newtim = tim + duration;

    /* How many days in duration? */
    days = newtim / MINUTES_PER_DAY;
    newtim = newtim % MINUTES_PER_DAY;
    h2 = newtim/60;
    min2 = newtim % 60;
    if (h2 == 0)      hh2=12;
    else if (h2 > 12) hh2=h2-12;
    else              hh2 = h2;

    if (days) {
	sprintf(daybuf, "+%d", days);
    } else {
	daybuf[0] = 0;
    }

    if (h >= 12) {
	ampm1 = DynamicPm;
    } else {
	ampm1 = DynamicAm;
    }
    if (h2 >= 12) {
	ampm2 = DynamicPm;
    } else {
	ampm2 = DynamicAm;
    }
    if (!days) {
	if (!strcmp(ampm1, ampm2)) {
	    ampm1 = "";
	}
    }

    switch(ScFormat) {
    case SC_AMPM:
	sprintf(buf, "%d%c%02d%s-%d%c%02d%s%s ",
		hh, TimeSep, min, ampm1, hh2, TimeSep, min2, ampm2, daybuf);
	break;

    case SC_MIL:
	sprintf(buf, "%02d%c%02d-%02d%c%02d%s ",
		h, TimeSep, min, h2, TimeSep, min2, daybuf);
	break;
    }
    return buf;
}

/***************************************************************/
/*                                                             */
/*  SimpleTime                                                 */
/*                                                             */
/*  Format the time according to simple time format.           */
/*  Answer is returned in a static buffer.                     */
/*  A trailing space is always added.                          */
/*                                                             */
/***************************************************************/
char const *SimpleTime(int tim)
{
    static char buf[32];
    int h, min, hh;

    buf[0] = 0;

    switch(ScFormat) {

    case SC_AMPM:
	if (tim != NO_TIME) {
	    h = tim / 60;
	    min = tim % 60;
	    if (h == 0) hh=12;
	    else if (h > 12) hh=h-12;
	    else hh=h;
	    sprintf(buf, "%d%c%02d%s ", hh, TimeSep, min, (h>=12) ? DynamicPm : DynamicAm);
	}
	break;

    case SC_MIL:
	if (tim != NO_TIME) {
	    h = tim / 60;
	    min = tim % 60;
	    sprintf(buf, "%02d%c%02d ", h, TimeSep, min);
	}
	break;
    }
    return buf;
}

/***************************************************************/
/*                                                             */
/*  SortCol                                                    */
/*                                                             */
/*  Sort the calendar entries in a column by time and priority */
/*                                                             */
/***************************************************************/
static void SortCol(CalEntry **col)
{
    CalEntry *cur, *prev, *next;

    cur = *col;
    prev = NULL;

/* Note that we use <= comparison rather than > comparison to preserve the
   file order of reminders which have the same time and priority */

    while (cur->next &&
	   CompareRems(0, cur->time, cur->priority,
		       0, cur->next->time, cur->next->priority,
		       SortByDate, SortByTime, SortByPrio, UntimedBeforeTimed) <= 0) {
	next = cur->next;
	/* Swap cur and next */
	if (!prev) {
	    *col = next;
	    cur->next = next->next;
	    next->next = cur;
	    prev = next;
	} else {
	    prev->next = next;
	    cur->next = next->next;
	    next->next = cur;
	    prev = next;
	}
    }
}

char const *SynthesizeTag(void)
{
    struct MD5Context ctx;
    unsigned char buf[16];
    static char out[128];
    MD5Init(&ctx);
    MD5Update(&ctx, (unsigned char *) CurLine, strlen(CurLine));
    MD5Final(buf, &ctx);
    sprintf(out, "__syn__%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
	    (unsigned int) buf[0], (unsigned int) buf[1],
	    (unsigned int) buf[2], (unsigned int) buf[3],
	    (unsigned int) buf[4], (unsigned int) buf[5],
	    (unsigned int) buf[6], (unsigned int) buf[7],
	    (unsigned int) buf[8], (unsigned int) buf[9],
	    (unsigned int) buf[10], (unsigned int) buf[11],
	    (unsigned int) buf[12], (unsigned int) buf[13],
	    (unsigned int) buf[14], (unsigned int) buf[15]);
    return out;
}

