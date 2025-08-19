/***************************************************************/
/*                                                             */
/*  CUSTOM.H.IN                                                */
/*                                                             */
/*  Contains various configuration parameters for Remind       */
/*  which you can customize.                                   */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

/*---------------------------------------------------------------------*/
/* DEFAULT_LATITUDE: Latitude of your location                         */
/* DEFAULT_LONGITUDE: Longitude of your location                       */
/* LOCATION: A string identifying your location.                       */
/* Latitude and longitude should be positive for the                   */
/* northern and eastern hemisphere and negative for the southern and   */
/* western hemisphere.                                                 */
/*                                                                     */
/* The default values are initially set to the city hall in Ottawa,    */
/* Ontario, Canada.                                                    */
/*---------------------------------------------------------------------*/
#define DEFAULT_LATITUDE 45.42055555555555
#define DEFAULT_LONGITUDE -75.68944444444445
#define LOCATION "Ottawa"

/*---------------------------------------------------------------------*/
/* DEFAULT_PAGE:  The default page size to use for Rem2PS.             */
/* The Letter version is appropriate for North America; the A4 version */
/* is appropriate for Europe.                                          */
/*---------------------------------------------------------------------*/
#define DEFAULT_PAGE {"Letter", 612, 792}
/* #define DEFAULT_PAGE {"A4", 595, 842} */

/*---------------------------------------------------------------------*/
/* DATESEP:  The default date separator.  Standard usage is '-';       */
/* others may prefer '/'.                                              */
/*---------------------------------------------------------------------*/
#define DATESEP '-'
/* #define DATESEP '/' */

/*---------------------------------------------------------------------*/
/* TIMESEP:  The default time separator.  North American usage is ':'; */
/* others may prefer '.'.                                              */
/*---------------------------------------------------------------------*/
#define TIMESEP ':'
/* #define TIMESEP '.' */

/*---------------------------------------------------------------------*/
/* DATETIMESEP:  The default datetime separator.  Default is '@';      */
/* others may prefer 'T'.                                              */
/*---------------------------------------------------------------------*/
#define DATETIMESEP '@'
/* #define DATETIMESEP '/' */

/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
/* You most likely do NOT have to tweak anything after this!          */
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/

/*---------------------------------------------------------------------*/
/* BASE: The base year for date calculation.  NOTE!  January 1 of the  */
/*       base year MUST be a Monday, else Remind will not work!        */
/*       IMPORTANT NOTE:  The Hebrew date routines depend on BASE      */
/*       being set to 1990.  If you change it, you'll have to add the  */
/*       number of days between 1 Jan <NEWBASE> and 1 Jan 1990 to the  */
/*       manifest constant CORRECTION in hbcal.c.  Also, the year      */
/*       folding mechanism in main.c depends on BASE<2001.             */
/*---------------------------------------------------------------------*/
#define BASE 1990

/*---------------------------------------------------------------------*/
/* YR_RANGE: The range of years allowed.  With 32-bit signed integers, */
/* the DATETIME type can store 2^31 minutes or about 4074 years.       */
/*---------------------------------------------------------------------*/
#define YR_RANGE 4000

/*---------------------------------------------------------------------*/
/* VAR_NAME_LEN: The maximum length of variable names.  Don't make it  */
/*               any less than 12.                                     */
/*---------------------------------------------------------------------*/
#define VAR_NAME_LEN 64

/*---------------------------------------------------------------------*/
/* MAX_PRT_LEN: The maximum number of characters to print when         */
/* displaying a string value for debugging purposes.                   */
/*---------------------------------------------------------------------*/
#define MAX_PRT_LEN 40

/*---------------------------------------------------------------------*/
/* MAX_STR_LEN: If non-zero, Remind will limit the maximum length      */
/* of string values to avoid eating up all of memory...                */
/*---------------------------------------------------------------------*/
#define MAX_STR_LEN 65535

/*---------------------------------------------------------------------*/
/* INCLUDE_NEST: How many nested INCLUDES do we handle?                */
/*---------------------------------------------------------------------*/
#define INCLUDE_NEST 9

/*---------------------------------------------------------------------*/
/* How many attempts to resolve a weird date spec?                     */
/*---------------------------------------------------------------------*/
#define TRIG_ATTEMPTS 500

/*---------------------------------------------------------------------*/
/* How many global omits of the form YYYY MM DD do we handle?          */
/*---------------------------------------------------------------------*/
#define MAX_FULL_OMITS 1000

/*---------------------------------------------------------------------*/
/* How many global omits of the form MM DD do we handle?               */
/*---------------------------------------------------------------------*/
#define MAX_PARTIAL_OMITS 366

/*---------------------------------------------------------------------*/
/* A newline - some systems need "\n\r"                                */
/*---------------------------------------------------------------------*/
#define NL "\n"

/*---------------------------------------------------------------------*/
/* Minimum number of linefeeds in each calendar "box"                  */
/*---------------------------------------------------------------------*/
#define CAL_LINES 5

/*---------------------------------------------------------------------*/
/* Don't change the next definitions                                   */
/*---------------------------------------------------------------------*/

/*---------------------------------------------------------------------*/
/* TAG_LEN: The maximum length of tags.  Don't change it               */
/*---------------------------------------------------------------------*/
#define TAG_LEN 48

#define PASSTHRU_LEN 32

#define MAX_RECURSION_LEVEL 1000

#define MAX_FUNC_ARGS 64

#define PSBEGIN "# rem2ps begin"
#define PSEND   "# rem2ps end"

#define PSBEGIN2 "# rem2ps2 begin"
#define PSEND2   "# rem2ps2 end"

#if defined(HAVE_MBSTOWCS) && defined(HAVE_WCTYPE_H)
#define REM_USE_WCHAR 1
#else
#undef REM_USE_WCHAR
#endif

#if defined(HAVE_READLINE) && defined(HAVE_READLINE_READLINE_H)
#define USE_READLINE 1
#else
#undef USE_READLINE
#endif

#if defined(HAVE_READLINE) && defined(HAVE_READLINE_READLINE_H) && defined(HAVE_READLINE_HISTORY_H)
#define USE_READLINE_HISTORY 1
#else
#undef USE_READLINE_HISTORY
#endif
