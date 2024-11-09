/***************************************************************/
/*                                                             */
/*  GERMAN.H                                                   */
/*                                                             */
/*  Support for the German language.                           */
/*                                                             */
/*  This file was derived from a patch submitted by Wolfgang   */
/*  Thronicke.  I don't guarantee that there are no mistakes - */
/*  I don't speak German.                                      */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2024 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/* The very first define in a language support file must be L_LANGNAME: */
#define L_LANGNAME "German"

/* Day names */
#define L_SUNDAY "Sonntag"
#define L_MONDAY "Montag"
#define L_TUESDAY "Dienstag"
#define L_WEDNESDAY "Mittwoch"
#define L_THURSDAY "Donnerstag"
#define L_FRIDAY "Freitag"
#define L_SATURDAY "Samstag"

/* Month names */
#define L_JAN "Januar"
#define L_FEB "Februar"
#define L_MAR "März"
#define L_APR "April"
#define L_MAY "Mai"
#define L_JUN "Juni"
#define L_JUL "Juli"
#define L_AUG "August"
#define L_SEP "September"
#define L_OCT "Oktober"
#define L_NOV "November"
#define L_DEC "Dezember"

/* Today and tomorrow */
#define L_TODAY "heute"
#define L_TOMORROW "morgen"

/* The default banner */
#define L_BANNER "Termine für %w, den %d. %m %y%o:"

/* "am" and "pm" */
#define L_AM "am"
#define L_PM "pm"

/* Ago and from now */
#define L_AGO "vorher"
#define L_FROMNOW "von heute"

/* "in %d days' time" */
#define L_INXDAYS "in %d Tagen"

/* "on" as in "on date..." */
#define L_ON "am"

/* Pluralizing - this is a problem for many languages and may require
   a more drastic fix */
#define L_PLURAL "en"

/* Minutes, hours, at, etc */
#define L_NOW "jetzt"
#define L_AT "um"
#define L_MINUTE "Minute"
#define L_HOUR "Stunde"
#define L_IS "ist"
#define L_WAS "war"
#define L_AND "und"
/* What to add to make "hour" plural */
#define L_HPLU "n"  
/* What to add to make "minute" plural */
#define L_MPLU "n"

/* Define any overrides here, such as L_ORDINAL_OVERRIDE, L_A_OVER, etc.
   See the file dosubst.c for more info. */
#define L_AMPM_OVERRIDE(ampm, hour)     ampm = (hour < 12) ? (hour<5) ? " nachts" : " vormittags" : (hour > 17) ? " abends" : " nachmittags";
#define L_ORDINAL_OVERRIDE              plu = ".";
#define L_A_OVER                        if (altmode == '*') { sprintf(s, "%s, den %d. %s %d", DayName[dse%7], d, MonthName[m], y); } else { sprintf(s, "%s %s, den %d. %s %d", L_ON, DayName[dse%7], d, MonthName[m], y); }
#define L_G_OVER                        if (altmode == '*') { sprintf(s, "%s, den %d. %s", DayName[dse%7], d, MonthName[m]); } else { sprintf(s, "%s %s, den %d. %s", L_ON, DayName[dse%7], d, MonthName[m]); }
#define L_U_OVER                        L_A_OVER
#define L_V_OVER                        L_G_OVER
