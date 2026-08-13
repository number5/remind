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

/***************************************************************/
/*                                                             */
/* MarkupToAnsi - Return the ANSI sequence to realize a markup */
/*                                                             */
/* This function may return a pointer to a static buffer that  */
/* is OVERWRITTEN each time it's called!                       */
/*                                                             */
/***************************************************************/
char const *
MarkupToAnsi(Markup *m)
{
    /* If no -@ command-line option, no markup. */
    if (!UseVTColors) {
        return "";
    }

    /* A NULL arg means we want the ANSI return-to-normal sequence */
    if (!m) {
        return "\x1B[0m";
    }
    switch(m->type) {
    case MARKUP_BOLD:
        return "\x1B[1m";
    case MARKUP_ITALIC:
        return "\x1B[3m";
    case MARKUP_COLOR:
        return Colorize(m->r, m->g, m->b, 0, 0);
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
Markup *
PushMarkup(Markup *stack, unsigned char type,
           unsigned char r, unsigned char g, unsigned char b)
{
    Markup *m = NEW(Markup);
    if (!m) return NULL;

    m->next = stack;
    m->type = type;
    m->r = r;
    m->g = g;
    m->b = b;
    return m;
}

/***************************************************************/
/*                                                             */
/* PopMarkup - Pop a markup item off of the stack.             */
/*                                                             */
/* Reurns a pointer to new top of stack                        */
/*                                                             */
/***************************************************************/
Markup *
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


/* We output markup in reverse order.  Recursive solution is kind
   of nasty, but we should not have super-deep recursion */
static void
OutputMarkupAux(Markup *stack, DynamicBuffer *output)
{
    char const *s;
    if (stack) {
        OutputMarkupAux(stack->next, output);
    }
    s = MarkupToAnsi(stack);
    OUTPUT(s);
}

/***************************************************************/
/*                                                             */
/* ParseFormatTag - parse a formatting tag and adjust markup   */
/*                  stack.                                     */
/*                                                             */
/***************************************************************/
wchar_t const *
ParseFormatTag(wchar_t const *ws, Markup *stack, Markup **newstack)
{
    int end_tag = 0;
    DynamicBuffer tag;
    char const *t;
    DBufInit(&tag);
    Markup rep;

    unsigned int r, g, b;

    *newstack = stack;

    /* Skip the opening < */
    ws++;
    while (*ws && iswspace(*ws)) {
        ws++;
    }
    while (*ws && (*ws != L'>')) {
        PutWideChar(*ws, &tag);
        ws++;
    }
    if (!*ws) {
        /* Unclosed tag... punt */
        DBufFree(&tag);
        return ws;
    }

    /* Skip closing '>' */
    ws++;

    rep.r = 0;
    rep.g = 0;
    rep.b = 0;

    t = DBufValue(&tag);
    if (*t == '/') {
        end_tag = 1;
        t++;
    }
    if (!strcasecmp(t, "i")) {
        rep.type = MARKUP_ITALIC;
    } else if (!strcasecmp(t, "b")) {
        rep.type = MARKUP_BOLD;
    } else if (end_tag && !strcasecmp(t, "span")) {
        rep.type = MARKUP_COLOR;
    } else if (sscanf(t, "span color=\"#%2x%2x%2x\"", &r, &g, &b) == 3 ||
               sscanf(t, "span fgcolor=\"#%2x%2x%2x\"", &r, &g, &b) == 3 ||
               sscanf(t, "span foreground=\"#%2x%2x%2x\"", &r, &g, &b) == 3) {
        rep.type = MARKUP_COLOR;
        rep.r = r;
        rep.g = g;
        rep.b = b;
    } else {
        DBufFree(&tag);
        return ws;
    }
    DBufFree(&tag);

    if (end_tag) {
        if (stack->type != rep.type) {
            /* mismatched tag... do nothing */
            return ws;
        }
        *newstack = PopMarkup(stack);
        return ws;
    }
    *newstack = PushMarkup(stack, rep.type, rep.r, rep.g, rep.b);
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

    /* First, reset attributes */
    s = MarkupToAnsi(NULL);
    OUTPUT(s);
    OutputMarkupAux(stack, output);
}

wchar_t const *
OutputHTMLEntity(wchar_t const *ws, DynamicBuffer *output)
{
    if (towlower(ws[0]) == L'l' && towlower(ws[1]) == L't' && towlower(ws[2]) == ';') {
        OUTPUT("<");
        return ws+3;
    }
    if (towlower(ws[0]) == L'g' && towlower(ws[1]) == L't' && towlower(ws[2]) == ';') {
        OUTPUT(">");
        return ws+3;
    }
    if (towlower(ws[0]) == L'a' && towlower(ws[1]) == L'm' && towlower(ws[2]) == 'p' && towlower(ws[3]) == ';') {
        OUTPUT("&");
        return ws+3;
    }
    PutWideChar(*ws, output);
    return ws+1;
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

    while(*ws) {
        if (*ws != L'<' && *ws != L'&') {
            PutWideChar(*ws, output);
            ws++;
            continue;
        }
        if (*ws == L'&') {
            ws++;
            ws = OutputHTMLEntity(ws, output);
            continue;
        }
        /* We have a tag here */
        ws = ParseFormatTag(ws, stack, &newstack);
        if (stack != newstack) {
            stack = newstack;
            OutputMarkup(stack, output);
        }
    }

    /* Delete any cruft left on the markup stack */
    if (stack) {
        /* Reset colors, etc */
        OutputMarkup(NULL, output);
    }
    while (stack) {
        stack = PopMarkup(stack);
    }
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

    while(isspace(*s)) s++;
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
        return E_BAD_MB_SEQ;
    }

    ws = calloc(len+1, sizeof(wchar_t));
    if (!ws) {
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
