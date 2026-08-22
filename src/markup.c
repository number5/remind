/***************************************************************/
/*                                                             */
/*  MARKUP.C                                                   */
/*                                                             */
/*  Functions for manipulating SPECIAL FORMATTED reminders     */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2026 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/
#include "config.h"
#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"
#include <stdlib.h>
#include <string.h>

#define OUTPUT(str) do { if(output) { DBufPuts(output, str); } else { fputs(str, stdout); } }while (0)

static char const *
MarkupName(Markup const *m)
{
    switch(m->type) {
    case MARKUP_BOLD:
        return "b";
    case MARKUP_ITALIC:
        return "i";
    case MARKUP_SPAN:
        return "span";
    case MARKUP_STRIKETHRU:
        return "s";
    case MARKUP_UNDERLINE:
        return "u";
    default:
        return "?";
    }
}

static int warned_about_mixed_case = 0;

/***************************************************************/
/*                                                             */
/* MarkupToAnsi - Return the ANSI sequence to realize a markup */
/*                                                             */
/* This function may return a pointer to a static buffer that  */
/* is OVERWRITTEN each time it's called!                       */
/*                                                             */
/***************************************************************/
static char const *
MarkupToAnsi(Markup *m, unsigned char *done)
{
    static char buf[64];


    /* If --no-fmt or no -@ command-line option, no markup. */
    if (NoFormat || !UseVTColors) {
        return "";
    }

    memset(buf, 0, sizeof(buf));

    /* A NULL arg means we want the ANSI return-to-normal sequence */
    if (!m) {
        return "\x1B[0m";
    }

    /* For real attributes, we REQUIRE a done memo area */
    if (!done) {
        Wprint("??? Software error!  No done scratchpad in MarkupToAnsi");
        return "";
    }

    switch(m->type) {
    case MARKUP_BOLD:
        if (done[MU_DONEBOLD]) {
            return "";
        }
        done[MU_DONEBOLD] = 1;
        return "\x1B[1m";
    case MARKUP_ITALIC:
        if (done[MU_DONEITALIC]) {
            return "";
        }
        done[MU_DONEITALIC] = 1;
        return "\x1B[3m";
    case MARKUP_SPAN:
        if (m->r >= 0 && m->g >= 0 && m->b >= 0) {
            if (!done[MU_DONECOLOR]) {
                strncat(buf, Colorize(m->r, m->g, m->b, 0, 0), sizeof(buf) - strlen(buf) - 1);
            }
            done[MU_DONECOLOR] = 1;
        }
        if (m->br >= 0 && m->bg >= 0 && m->bb >= 0) {
            if (!done[MU_DONEBGCOLOR]) {
                strncat(buf, Colorize(m->br, m->bg, m->bb, 1, 0), sizeof(buf) - strlen(buf) - 1);
            }
            done[MU_DONEBGCOLOR] = 1;
        }
        return buf;
    case MARKUP_STRIKETHRU:
        if (done[MU_DONESTRIKETHRU]) {
            return "";
        }
        done[MU_DONESTRIKETHRU] = 1;
        return "\x1B[9m";
    case MARKUP_UNDERLINE:
        if (done[MU_DONEUNDERLINE]) {
            return "";
        }
        done[MU_DONEUNDERLINE] = 1;
        return "\x1B[4m";
    }
    return "";
}

/***************************************************************/
/*                                                             */
/* PushMarkup - Push a markup item onto the stack.             */
/*                                                             */
/* Reurns a pointer to new top of stack or NULL if out of      */
/* memory.                                                     */
/*                                                             */
/***************************************************************/
static Markup *
PushMarkup(Markup *stack, Markup const *p)
{
    Markup *m = NEW(Markup);
    if (!m) return NULL;
    *m = *p;
    m->next = stack;
    return m;
}

/***************************************************************/
/*                                                             */
/* PopMarkup - Pop a markup item off of the stack.             */
/*                                                             */
/* Reurns a pointer to new top of stack                        */
/*                                                             */
/***************************************************************/
static Markup *
PopMarkup(Markup *stack)
{
    Markup *m;

    if (!stack) {
        return NULL;
    }
    m = stack;
    stack = stack->next;
    free(m);
    return stack;
}

/***************************************************************/
/*                                                             */
/* FreeMarkupSatck - free all the markup items                 */
/*                                                             */
/***************************************************************/
void
FreeMarkupStack(Markup *stack)
{
    if ((DebugFlag & DB_FORMAT) && stack) {
        WprintOnce("Unclosed formatting tags");
    }
    while(stack) {
        stack = PopMarkup(stack);
    }
}

/* We only want the most recently-stacked opening markup tag of each type
   output.  So we keep track of which ones we've done in the "done" array */
static void
OutputMarkupAux(Markup *stack, DynamicBuffer *output, unsigned char *done)
{
    char const *s;
    while(stack) {
        s = MarkupToAnsi(stack, done);
        OUTPUT(s);
        stack = stack->next;
    }
}

static void
ParseAttributes(char const *t, Markup *m) {
    unsigned int r, g, b;
    int gobbled = 0;
    int parsing = 1;

    while (parsing) {
        parsing = 0;
        while (*t && isspace((unsigned char) *t)) {
            t++;
        }
        if (!*t) {
            return;
        }
        if (sscanf(t, "color = \"#%2x%2x%2x\" %n", &r, &g, &b, &gobbled) == 3) {
            if (gobbled > 0) {
                parsing = 1;
            }
        } else if (sscanf(t, "fgcolor = \"#%2x%2x%2x\" %n", &r, &g, &b, &gobbled) == 3) {
            if (gobbled > 0) {
                parsing = 1;
            }
        } else if (sscanf(t, "foreground = \"#%2x%2x%2x\" %n", &r, &g, &b, &gobbled) == 3) {
            if (gobbled > 0) {
                parsing = 1;
            }
        }
        if (parsing == 1) {
            m->r = r;
            m->g = g;
            m->b = b;
            t += gobbled;
            continue;
        }
        if (sscanf(t, "bgcolor = \"#%2x%2x%2x\" %n", &r, &g, &b, &gobbled) == 3) {
            if (gobbled > 0) {
                parsing = 1;
            }
        } else if (sscanf(t, "background = \"#%2x%2x%2x\" %n", &r, &g, &b, &gobbled) == 3) {
            if (gobbled > 0) {
                parsing = 1;
            }
        }
        if (parsing == 1) {
            m->br = r;
            m->bg = g;
            m->bb = b;
            t += gobbled;
            continue;
        }
        while (*t && isspace((unsigned char) *t)) {
            t++;
        }
        if (!*t) {
            return;
        }
        if (DebugFlag & DB_FORMAT) {
            WprintOnce("Unknown attribute in <span> tag at %s", t);
        }
    }
    return;
}

/***************************************************************/
/*                                                             */
/* ParseFormatTag - parse a formatting tag and adjust markup   */
/*                  stack.                                     */
/*                                                             */
/***************************************************************/
static wchar_t const *
ParseFormatTag(wchar_t const *ws, Markup *stack, Markup **newstack)
{
    int end_tag = 0;
    DynamicBuffer tag;
    char const *t;
    DBufInit(&tag);
    Markup rep;
    wchar_t wc;
    int seen_quote = 0;

    if (newstack) {
        *newstack = stack;
    }

    /* Skip the opening < */
    ws++;
    while (*ws && iswspace(*ws)) {
        ws++;
    }
    while (*ws && (*ws != L'>')) {
        wc = towlower(*ws);
        if (wc == L'"') {
            seen_quote = !seen_quote;
        }
        if (DebugFlag & DB_FORMAT) {
            if (!seen_quote && wc != *ws) {
                if (!warned_about_mixed_case) {
                    WprintOnce("Markup tags and attribute names should be all lower-case");
                    warned_about_mixed_case = 1;
                }
            }
        }
        PutWideChar(wc, &tag);
        ws++;
    }
    if (!*ws) {
        /* Unclosed tag... punt */
        if (DebugFlag & DB_FORMAT) {
            WprintOnce("Tag is missing closing >");
        }
        DBufFree(&tag);
        return ws;
    }

    /* Skip closing '>' */
    ws++;
    if (seen_quote) {
        if (DebugFlag & DB_FORMAT) {
            WprintOnce("Unclosed quote in tag <%s>", DBufValue(&tag));
        }
        DBufFree(&tag);
        return ws;
    }

    rep.r = -1;
    rep.g = -1;
    rep.b = -1;
    rep.br = -1;
    rep.bg = -1;
    rep.bb = -1;

    t = DBufValue(&tag);
    if (*t == '/') {
        end_tag = 1;
        t++;
    }
    if (!strcasecmp(t, "i")) {
        rep.type = MARKUP_ITALIC;
    } else if (!strcasecmp(t, "b")) {
        rep.type = MARKUP_BOLD;
    } else if (!strcasecmp(t, "u")) {
        rep.type = MARKUP_UNDERLINE;
    } else if (!strcasecmp(t, "s")) {
        rep.type = MARKUP_STRIKETHRU;
    } else if (end_tag && !strcasecmp(t, "span")) {
        rep.type = MARKUP_SPAN;
    } else if (!strncasecmp(t, "span ", 5)) {
        rep.type = MARKUP_SPAN;
        ParseAttributes(t + 5, &rep);
    } else {
        if (DebugFlag & DB_FORMAT) {
            WprintOnce("Unsupported formatting tag <%s>", DBufValue(&tag));
        }
        DBufFree(&tag);
        return ws;
    }
    DBufFree(&tag);

    if (!newstack) {
        return ws;
    }

    if (end_tag) {
        if (!stack && !NoFormat) {
            if (DebugFlag & DB_FORMAT) {
                WprintOnce("</%s> tag without any opening tag",
                       MarkupName(&rep));
            }
            return ws;
        }
        if (!NoFormat && stack && stack->type != rep.type) {
            if (DebugFlag & DB_FORMAT) {
                WprintOnce("<%s> tag closed by </%s>",
                       MarkupName(stack), MarkupName(&rep));
            }
            /* mismatched tag... do nothing */
            return ws;
        }
        if (!NoFormat) {
            *newstack = PopMarkup(stack);
        }
        return ws;
    }
    if (!NoFormat) {
        *newstack = PushMarkup(stack, &rep);
    }
    return ws;
}

/***************************************************************/
/*                                                             */
/* OutputMarkup - output the entire markup stack               */
/*                                                             */
/***************************************************************/
void
OutputMarkup(Markup *stack, DynamicBuffer *output)
{
    char const *s;
    unsigned char done[MU_MAX_DONES];

    memset(done, 0, MU_MAX_DONES);

    if (NoFormat) {
        return;
    }
    /* First, reset attributes */
    s = MarkupToAnsi(NULL, NULL);
    OUTPUT(s);

    /* If we have a default color, do that first */
    if (UseVTColors && DefaultColorR >= 0 && DefaultColorG >= 0 && DefaultColorB >= 0) {
        OUTPUT(Colorize(DefaultColorR, DefaultColorG, DefaultColorB, 0, 0));
    }
    OutputMarkupAux(stack, output, done);
}

static wchar_t
GetHTMLEntity(wchar_t const *ws, wchar_t const **news)
{
    if (towlower(ws[1]) == L'l' && towlower(ws[2]) == L't' && towlower(ws[3]) == ';') {
        if (news) {
            *news = ws+4;
        }
        return L'<';
    }
    if (towlower(ws[1]) == L'g' && towlower(ws[2]) == L't' && towlower(ws[3]) == ';') {
        if (news) {
            *news = ws+4;
        }
        return L'>';
    }
    if (towlower(ws[1]) == L'a' && towlower(ws[2]) == L'm' && towlower(ws[3]) == 'p' && towlower(ws[4]) == ';') {
        if (news) {
            *news = ws+5;
        }
        return L'&';
    }
    if (news) {
        *news = ws+1;
    }
    return *ws;
}

/***************************************************************/
/*                                                             */
/* GetNextNonFormattingWC - get the next real character        */
/* from a SPECIAL FORMATTED reminder.  Update ws as well and   */
/* if stack is non-NULL, update the markup stack too           */
/*                                                             */
/***************************************************************/
wchar_t
GetNextNonFormattingWC(wchar_t const *ws, wchar_t const **news, Markup **stack, int *update_markup)
{
    Markup *newstack;
    if (update_markup) {
        *update_markup = 0;
    }
    while(*ws) {
        if (*ws != L'<' && *ws != L'&') {
            if (news) {
                *news = ws+1;
            }
            return *ws;
        }
        if (*ws == L'&') {
            return GetHTMLEntity(ws, news);
        }
        if (stack) {
            ws = ParseFormatTag(ws, *stack, &newstack);
            if (newstack != *stack) {
                if (update_markup) {
                    *update_markup = 1;
                }
                *stack = newstack;
            }
        } else {
            ws = ParseFormatTag(ws, NULL, NULL);
        }
    }
    if (news) {
        *news = ws;
    }
    return *ws;
}

/***************************************************************/
/*                                                             */
/* OutputFormattedReminderWC - output a SPECIAL FORMATTED body */
/*                                                             */
/* Here, body has been converted to wchar_t[]                  */
/*                                                             */
/***************************************************************/
int
OutputFormattedReminderWC(wchar_t const *ws, DynamicBuffer *output)
{
    Markup *stack = NULL;
    Markup *newstack = NULL;
    wchar_t wc;
    int update_markup;

    while ((wc = GetNextNonFormattingWC(ws, &ws, &newstack, &update_markup)) != 0) {
        if (newstack != stack) {
            stack = newstack;
        }
        if (update_markup) {
            OutputMarkup(stack, output);
        }
        PutWideChar(wc, output);
    }

    if (newstack != stack) {
        stack = newstack;
    }
    if (update_markup) {
        OutputMarkup(stack, output);
    }

    /* Reset colors, etc */
    if (stack || (UseVTColors && DefaultColorR >= 0 && DefaultColorG >= 0 && DefaultColorB >= 0)) {
        OUTPUT(Decolorize());
    }

    /* Delete any cruft left on the markup stack */
    if (stack) {
        FreeMarkupStack(stack);
    }

    OUTPUT("\n");
    return OK;
}

/***************************************************************/
/*                                                             */
/* OutputFormattedReminder - output a SPECIAL FORMATTED body   */
/*                                                             */
/***************************************************************/
int
OutputFormattedReminder(char const *url, char const *s, DynamicBuffer *output)
{
    int r = OK;
    wchar_t *ws;
    size_t len;

    warned_about_mixed_case = 0;
    while(isspace((unsigned char) *s)) s++;
    if (!*s) {
        return r;
    }
    if (url) {
        OUTPUT("\x1B]8;;");
        OUTPUT(url);
        OUTPUT("\x1B\\");
    }

    /* Convert to wchar_t */
    len = mbstowcs(NULL, s, 0);
    if (len == (size_t) -1) {
        if (url) {
            OUTPUT("\x1B]8;;\x1B\\");
        }
        return E_BAD_MB_SEQ;
    }

    ws = calloc(len+1, sizeof(wchar_t));
    if (!ws) {
        if (url) {
            OUTPUT("\x1B]8;;\x1B\\");
        }
        return E_NO_MEM;
    }
    len = mbstowcs(ws, s, len);
    if (len == (size_t) -1) {
        free(ws);
        return E_BAD_MB_SEQ;
    }

    r = OutputFormattedReminderWC(ws, output);
    free(ws);

    if (url) {
        OUTPUT("\x1B]8;;\x1B\\");
    }
    return r;
}
