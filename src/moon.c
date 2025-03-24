/***************************************************************/
/*                                                             */
/*  MOON.C                                                     */
/*                                                             */
/*  Calculations for figuring out moon phases.                 */
/*                                                             */
/*  This file is part of REMIND.                               */
/*  Copyright (C) 1992-2025 by Dianne Skoll                    */
/*  SPDX-License-Identifier: GPL-2.0-only                      */
/*                                                             */
/***************************************************************/

#include "config.h"

/* All of these routines were adapted from the program "moontool"
   by John Walker, February 1988.  Here's the blurb from moontool:

   ... The information is generally accurate to within ten
   minutes.

   The algorithms used in this program to calculate the positions Sun and
   Moon as seen from the Earth are given in the book "Practical Astronomy
   With Your Calculator" by Peter Duffett-Smith, Second Edition,
   Cambridge University Press, 1981. Ignore the word "Calculator" in the
   title; this is an essential reference if you're interested in
   developing software which calculates planetary positions, orbits,
   eclipses, and the like. If you're interested in pursuing such
   programming, you should also obtain:

   "Astronomical Formulae for Calculators" by Jean Meeus, Third Edition,
   Willmann-Bell, 1985. A must-have.

   "Planetary Programs and Tables from -4000 to +2800" by Pierre
   Bretagnon and Jean-Louis Simon, Willmann-Bell, 1986. If you want the
   utmost (outside of JPL) accuracy for the planets, it's here.

   "Celestial BASIC" by Eric Burgess, Revised Edition, Sybex, 1985. Very
   cookbook oriented, and many of the algorithms are hard to dig out of
   the turgid BASIC code, but you'll probably want it anyway.

   Many of these references can be obtained from Willmann-Bell, P.O. Box
   35025, Richmond, VA 23235, USA. Phone: (804) 320-7016. In addition
   to their own publications, they stock most of the standard references
   for mathematical and positional astronomy.

   This program was written by:

      John Walker
      Autodesk, Inc.
      2320 Marinship Way
      Sausalito, CA 94965
      (415) 332-2344 Ext. 829

      Usenet: {sun!well}!acad!kelvin

   This program is in the public domain: "Do what thou wilt shall be the
   whole of the law". I'd appreciate receiving any bug fixes and/or
   enhancements, which I'll incorporate in future versions of the
   program. Please leave the original attribution information intact so
   that credit and blame may be properly apportioned.

*/
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "types.h"
#include "protos.h"
#include "globals.h"
#include "err.h"

/* Function prototypes */
static long jdate (int y, int mon, int day);
static double jtime (int y, int mon, int day, int hour, int min, int sec);
static void jyear (double td, int *yy, int *mm, int *dd);
static void jhms (double j, int *h, int *m, int *s);
static double meanphase (double sdate, double phase, double *usek);
static double truephase (double k, double phase);
static double kepler (double m, double ecc);
static double phase (double, double *, double *, double *, double *, double *, double *);


/*  Astronomical constants  */

#define epoch       2444238.5      /* 1980 January 0.0 */

/*  Constants defining the Sun's apparent orbit  */

#define elonge      278.833540     /* Ecliptic longitude of the Sun
                                      at epoch 1980.0 */
#define elongp      282.596403     /* Ecliptic longitude of the Sun at
                                      perigee */
#define eccent      0.016718       /* Eccentricity of Earth's orbit */
#define sunsmax     1.495985e8     /* Semi-major axis of Earth's orbit, km */
#define sunangsiz   0.533128       /* Sun's angular size, degrees, at
                                      semi-major axis distance */

/*  Elements of the Moon's orbit, epoch 1980.0  */

#define mmlong      64.975464      /* Moon's mean lonigitude at the epoch */
#define mmlongp     349.383063     /* Mean longitude of the perigee at the
                                      epoch */
#define mecc        0.054900       /* Eccentricity of the Moon's orbit */
#define mangsiz     0.5181         /* Moon's angular size at distance a
                                      from Earth */
#define msmax       384401.0       /* Semi-major axis of Moon's orbit in km */
#define synmonth    29.53058868    /* Synodic month (new Moon to new Moon) */

#ifdef PI
#undef PI
#endif

#define PI 3.14159265358979323846

/*  Handy mathematical functions  */

#ifdef abs
#undef abs
#endif
#define abs(x) ((x) < 0 ? (-(x)) : (x))                   /* Absolute val */

#define fixangle(a) ((a) - 360.0 * (floor((a) / 360.0)))  /* Fix angle    */
#define torad(d) ((d) * (PI / 180.0))                     /* Deg->Rad     */
#define todeg(d) ((d) * (180.0 / PI))                     /* Rad->Deg     */
#define dsin(x) (sin(torad((x))))                         /* Sin from deg */
#define dcos(x) (cos(torad((x))))                         /* Cos from deg */

/***************************************************************/
/*                                                             */
/*  jdate                                                      */
/*                                                             */
/*  Convert a date and time to Julian day and fraction.        */
/*                                                             */
/***************************************************************/
static long jdate(int y, int mon, int day)
{
    long c, m;

    m = mon+1;
    if (m>2) {
        m -= 3;
    } else {
        m += 9;
        y--;
    }
    c = y/100L;   /* Century */
    y -= 100L * c;
    return day + (c*146097L)/4 + (y*1461L)/4 + (m*153L+2)/5 + 1721119L;
}

/***************************************************************/
/*                                                             */
/*  jtime                                                      */
/*                                                             */
/*  Convert a GMT date and time to astronomical Julian time,   */
/*  i.e. Julian date plus day fraction, expressed as a double  */
/*                                                             */
/***************************************************************/
static double jtime(int y, int mon, int day, int hour, int min, int sec)
{
    return (jdate(y, mon, day)-0.5) +
        (sec + 60L * (long) min + 3600L * (long) hour) / 86400.0;
}

/***************************************************************/
/*                                                             */
/*  jyear                                                      */
/*                                                             */
/*  Convert a Julian date to year, month, day.                 */
/*                                                             */
/***************************************************************/
static void jyear(double td, int *yy, int *mm, int *dd)
{
    double j, d, y, m;

    td += 0.5;         /* Astronomical to civil */
    j = floor(td);
    j = j - 1721119.0;
    y = floor(((4 * j) - 1) / 146097.0);
    j = (j * 4.0) - (1.0 + (146097.0 * y));
    d = floor(j / 4.0);
    j = floor(((4.0 * d) + 3.0) / 1461.0);
    d = ((4.0 * d) + 3.0) - (1461.0 * j);
    d = floor((d + 4.0) / 4.0);
    m = floor(((5.0 * d) - 3) / 153.0);
    d = (5.0 * d) - (3.0 + (153.0 * m));
    d = floor((d + 5.0) / 5.0);
    y = (100.0 * y) + j;
    if (m < 10.0)
        m = m + 2;
    else {
        m = m - 10;
        y = y + 1;
    }
    *yy = y;
    *mm = m;
    *dd = d;
}

/***************************************************************/
/*                                                             */
/*  jhms                                                       */
/*                                                             */
/*  Convert a Julian time to hour, minutes and seconds.        */
/*                                                             */
/***************************************************************/
static void jhms(double j, int *h, int *m, int *s)
{
    long ij;

    j += 0.5;         /* Astronomical to civil */
    ij = (j - floor(j)) * 86400.0;
    *h = ij / 3600L;
    *m = (ij / 60L) % 60L;
    *s = ij % 60L;
}

/***************************************************************/
/*                                                             */
/*  meanphase                                                  */
/*                                                             */
/*  Calculates mean phase of the Moon for a                    */
/*  given base date and desired phase:                         */
/*     0.0   New Moon                                          */
/*     0.25  First quarter                                     */
/*     0.5   Full moon                                         */
/*     0.75  Last quarter                                      */
/*  Beware!!!  This routine returns meaningless                */
/*  results for any other phase arguments.  Don't              */
/*  attempt to generalise it without understanding             */
/*  that the motion of the moon is far more complicated        */
/*  than this calculation reveals.                             */
/*                                                             */
/***************************************************************/
static double meanphase(double sdate, double phase, double *usek)
{
    double k, t, t2, t3, nt1;

/*** The following was the original code:  It gave roundoff errors
  causing moonphase info to fail for Dec 1994.  ***/
/*    jyear(sdate, &yy, &mm, &dd);
      k = (yy + (mm/12.0) - 1900) * 12.368531; */

/*** The next line is the replacement ***/
    k = (sdate - 2415020.0) / synmonth;

    /* Time in Julian centuries from 1900 January 0.5 */
    t = (sdate - 2415020.0) / 36525.0;
    t2 = t * t;            /* Square for frequent use */
    t3 = t2 * t;                   /* Cube for frequent use */

    *usek = k = floor(k) + phase;
    nt1 = 2415020.75933 + synmonth * k
        + 0.0001178 * t2
        - 0.000000155 * t3
        + 0.00033 * dsin(166.56 + 132.87 * t - 0.009173 * t2);

    return nt1;
}

/***************************************************************/
/*                                                             */
/*  truephase                                                  */
/*                                                             */
/*  Given a K value used to determine the                      */
/*  mean phase of the new moon, and a phase                    */
/*  selector (0.0, 0.25, 0.5, 0.75), obtain                    */
/*  the true, corrected phase time.                            */
/*                                                             */
/***************************************************************/
static double truephase(double k, double phase)
{
    double t, t2, t3, pt, m, mprime, f;
    int apcor = 0;

    k += phase;            /* Add phase to new moon time */
    t = k / 1236.8531;     /* Time in Julian centuries from
                              1900 January 0.5 */
    t2 = t * t;            /* Square for frequent use */
    t3 = t2 * t;                   /* Cube for frequent use */
    pt = 2415020.75933     /* Mean time of phase */
        + synmonth * k
        + 0.0001178 * t2
        - 0.000000155 * t3
        + 0.00033 * dsin(166.56 + 132.87 * t - 0.009173 * t2);

    m = 359.2242               /* Sun's mean anomaly */
        + 29.10535608 * k
        - 0.0000333 * t2
        - 0.00000347 * t3;
    mprime = 306.0253          /* Moon's mean anomaly */
        + 385.81691806 * k
        + 0.0107306 * t2
        + 0.00001236 * t3;
    f = 21.2964                /* Moon's argument of latitude */
        + 390.67050646 * k
        - 0.0016528 * t2
        - 0.00000239 * t3;
    if ((phase < 0.01) || (abs(phase - 0.5) < 0.01)) {

        /* Corrections for New and Full Moon */

        pt +=     (0.1734 - 0.000393 * t) * dsin(m)
            + 0.0021 * dsin(2 * m)
            - 0.4068 * dsin(mprime)
            + 0.0161 * dsin(2 * mprime)
            - 0.0004 * dsin(3 * mprime)
            + 0.0104 * dsin(2 * f)
            - 0.0051 * dsin(m + mprime)
            - 0.0074 * dsin(m - mprime)
            + 0.0004 * dsin(2 * f + m)
            - 0.0004 * dsin(2 * f - m)
            - 0.0006 * dsin(2 * f + mprime)
            + 0.0010 * dsin(2 * f - mprime)
            + 0.0005 * dsin(m + 2 * mprime);
        apcor = 1;
    } else if ((abs(phase - 0.25) < 0.01 || (abs(phase - 0.75) < 0.01))) {
        pt +=     (0.1721 - 0.0004 * t) * dsin(m)
            + 0.0021 * dsin(2 * m)
            - 0.6280 * dsin(mprime)
            + 0.0089 * dsin(2 * mprime)
            - 0.0004 * dsin(3 * mprime)
            + 0.0079 * dsin(2 * f)
            - 0.0119 * dsin(m + mprime)
            - 0.0047 * dsin(m - mprime)
            + 0.0003 * dsin(2 * f + m)
            - 0.0004 * dsin(2 * f - m)
            - 0.0006 * dsin(2 * f + mprime)
            + 0.0021 * dsin(2 * f - mprime)
            + 0.0003 * dsin(m + 2 * mprime)
            + 0.0004 * dsin(m - 2 * mprime)
            - 0.0003 * dsin(2 * m + mprime);
        if (phase < 0.5)
            /* First quarter correction */
            pt += 0.0028 - 0.0004 * dcos(m) + 0.0003 * dcos(mprime);
        else
            /* Last quarter correction */
            pt += -0.0028 + 0.0004 * dcos(m) - 0.0003 * dcos(mprime);
        apcor = 1;
    }
    if (!apcor) return 0.0;
    return pt;
}

/***************************************************************/
/*                                                             */
/*  kepler                                                     */
/*                                                             */
/*  Solve the equation of Kepler.                              */
/*                                                             */
/***************************************************************/
static double kepler(double m, double ecc)
{
    double e, delta;
#define EPSILON 1E-6

    e = m = torad(m);
    do {
        delta = e - ecc * sin(e) - m;
        e -= delta / (1 - ecc * cos(e));
    } while (abs(delta) > EPSILON);
    return e;
}
/***************************************************************/
/*                                                             */
/*  PHASE  --  Calculate phase of moon as a fraction:          */
/*                                                             */
/*   The argument is the time for which the phase is           */
/*   Requested, expressed as a Julian date and                 */
/*   fraction.  Returns the terminator phase angle as a        */
/*   percentage of a full circle (i.e., 0 to 1), and           */
/*   stores into pointer arguments the illuminated             */
/*   fraction of the Moon's disc, the Moon's age in            */
/*   days and fraction, the distance of the Moon from          */
/*   the centre of the Earth, and the angular diameter         */
/*   subtended by the Moon as seen by an observer at           */
/*   the centre of the Earth.                                  */
/*                                                             */
/***************************************************************/
static double phase(double pdate,
                    double *pphase,
                    double *mage,
                    double *dist,
                    double *angdia,
                    double *sudist,
                    double *suangdia)
{

    double Day, N, M, Ec, Lambdasun, ml, MM, Ev, Ae, A3, MmP,
        mEc, A4, lP, V, lPP,
        MoonAge, Phase,
        MoonDist, MoonDFrac, MoonAng,
        F, SunDist, SunAng;

    /* Calculation of the Sun's position */

    Day = pdate - epoch;            /* Date within epoch */
    N = fixangle((360 / 365.2422) * Day); /* Mean anomaly of the Sun */
    M = fixangle(N + elonge - elongp);    /* Convert from perigee
                                             coordinates to epoch 1980.0 */
    Ec = kepler(M, eccent);     /* Solve equation of Kepler */
    Ec = sqrt((1 + eccent) / (1 - eccent)) * tan(Ec / 2);
    Ec = 2 * todeg(atan(Ec));   /* 1 anomaly */
    Lambdasun = fixangle(Ec + elongp);  /* Sun's geocentric ecliptic
                                           longitude */
    /* Orbital distance factor */
    F = ((1 + eccent * cos(torad(Ec))) / (1 - eccent * eccent));
    SunDist = sunsmax / F;          /* Distance to Sun in km */
    SunAng = F * sunangsiz;     /* Sun's angular size in degrees */


    /* Calculation of the Moon's position */

    /* Moon's mean longitude */
    ml = fixangle(13.1763966 * Day + mmlong);

    /* Moon's mean anomaly */
    MM = fixangle(ml - 0.1114041 * Day - mmlongp);

    /* Evection */
    Ev = 1.2739 * sin(torad(2 * (ml - Lambdasun) - MM));

    /* Annual equation */
    Ae = 0.1858 * sin(torad(M));

    /* Correction term */
    A3 = 0.37 * sin(torad(M));

    /* Corrected anomaly */
    MmP = MM + Ev - Ae - A3;

    /* Correction for the equation of the centre */
    mEc = 6.2886 * sin(torad(MmP));

    /* Another correction term */
    A4 = 0.214 * sin(torad(2 * MmP));

    /* Corrected longitude */
    lP = ml + Ev + mEc - Ae + A4;

    /* Variation */
    V = 0.6583 * sin(torad(2 * (lP - Lambdasun)));

    /* 1 longitude */
    lPP = lP + V;

    /* Calculation of the phase of the Moon */

    /* Age of the Moon in degrees */
    MoonAge = lPP - Lambdasun;

    /* Phase of the Moon */
    Phase = (1 - cos(torad(MoonAge))) / 2;

    /* Calculate distance of moon from the centre of the Earth */

    MoonDist = (msmax * (1 - mecc * mecc)) /
        (1 + mecc * cos(torad(MmP + mEc)));

    /* Calculate Moon's angular diameter */

    MoonDFrac = MoonDist / msmax;
    MoonAng = mangsiz / MoonDFrac;

    if(pphase)   *pphase = Phase;
    if(mage)     *mage = synmonth * (fixangle(MoonAge) / 360.0);
    if(dist)     *dist = MoonDist;
    if(angdia)   *angdia = MoonAng;
    if(sudist)   *sudist = SunDist;
    if(suangdia) *suangdia = SunAng;
    return fixangle(MoonAge) / 360.0;
}

/***************************************************************/
/*                                                             */
/*  MoonPhase                                                  */
/*                                                             */
/*  Interface routine dealing in Remind representations.       */
/*  Given a local date and time, returns the moon phase at     */
/*  that date and time as a number from 0 to 360.              */
/*                                                             */
/***************************************************************/
int MoonPhase(int date, int time)
{
    int utcd, utct;
    int y, m, d;
    double jd, mp;

    /* Convert from local to UTC */
    LocalToUTC(date, time, &utcd, &utct);

    /* Convert from Remind representation to year/mon/day */
    FromDSE(utcd, &y, &m, &d);

    /* Convert to a Julian date */
    jd = jtime(y, m, d, (utct / 60), (utct % 60), 0);

    /* Calculate moon phase */
    mp = 360.0 * phase(jd, NULL, NULL, NULL, NULL, NULL, NULL);
    return (int) mp;
}

/***************************************************************/
/*                                                             */
/*  HuntPhase                                                  */
/*                                                             */
/*  Given a starting date and time and a target phase, find    */
/*  the first date on or after the starting date and time when */
/*  the moon hits the specified phase.  Phase must be from     */
/*  0 to 3 for new, 1stq, full, 3rdq                           */
/*                                                             */
/***************************************************************/
void HuntPhase(int startdate, int starttim, int phas, int *date, int *time)
{
    int utcd, utct;
    int y, m, d;
    int h, min, s;
    int d1, t1;
    double k1, k2, jd, jdorig;
    double nt1, nt2;

    /* Convert from local to UTC */
    LocalToUTC(startdate, starttim, &utcd, &utct);

    /* Convert from Remind representation to year/mon/day */
    FromDSE(utcd, &y, &m, &d);
    /* Convert to a true Julian date */
    jdorig = jtime(y, m, d, (utct / 60), (utct % 60), 0);
    jd = jdorig - 45.0;
    nt1 = meanphase(jd, 0.0, &k1);
    while(1) {
        jd += synmonth;
        nt2 = meanphase(jd, 0.0, &k2);
        if (nt1 <= jdorig && nt2 > jdorig) break;
        nt1 = nt2;
        k1 = k2;
    }
    jd = truephase(k1, phas/4.0);
    if (jd < jdorig) jd = truephase(k2, phas/4.0);

    /* Convert back to Remind format */
    jyear(jd, &y, &m, &d);
    jhms(jd, &h, &min, &s);

    d1 = DSE(y, m, d);
    t1 = h*60 + min;
    UTCToLocal(d1, t1, date, time);
}

/*
  Moonrise and Moonset calculations
  Derived from: https://github.com/signetica/MoonRise
  Original license from that project:

  Copyright 2007 Stephen R. Schmitt
  Subsequent work Copyright 2020 Cyrus Rahman

  You may use or modify this source code in any way you find useful,
  provided that you agree that the author(s) have no warranty,
  obligations or liability.  You must determine the suitability of this
  source code for your use.

  Redistributions of this source code must retain this copyright notice.
*/

/* How many hours to search for moonrise / moonset?  We search
   half a window on either side of the starting point */
#define MR_WINDOW 48

/* K1 tide cycle is 1.0027379 cycles per solar day */
#define K1 15*(PI/180)*1.0027379

#define remainder(x, y) ((x) - (y) * rint((x)/(y)))

struct MoonInfo {
    time_t queryTime;
    time_t riseTime;
    time_t setTime;
    double riseAz;
    double setAz;
    int hasRise;
    int hasSet;
    int isVisible;
};

static void init_moon_info(struct MoonInfo *info)
{
    info->queryTime = (time_t) 0;
    info->riseTime = (time_t) 0;
    info->setTime = (time_t) 0;
    info->riseAz = 0.0;
    info->setAz = 0.0;
    info->hasRise = 0;
    info->hasSet = 0;
    info->isVisible = 0;
}
/*
  Local Sidereal Time
  Provides local sidereal time in degrees, requires longitude in degrees
  and time in fractional Julian days since Jan 1, 2000, 1200UTC (e.g. the
  Julian date - 2451545).
  cf. USNO Astronomical Almanac and
  https://astronomy.stackexchange.com/questions/24859/local-sidereal-time
*/

static double local_sidereal_time(double offset_days, double longitude)
{
    double ltime = (15.0L * (6.697374558L + 0.06570982441908L * offset_days +
                             remainder(offset_days, 1) * 24 + 12 +
                             0.000026 * (offset_days / 36525) * (offset_days / 36525)) + longitude) / 360.0;
    ltime -= floor(ltime);
    return ltime * 360.0;
}

static double julian_from_time_t(time_t t)
{
    return ((double) t) / 86400.0L + 2440587.5L;
}

static time_t time_t_from_dse(int dse)
{
    int y, m, d;
    struct tm local;
    FromDSE(dse, &y, &m, &d);

    local.tm_sec = 0;
    local.tm_min = 0;
    local.tm_hour = 0;
    local.tm_mday = d;
    local.tm_mon = m;
    local.tm_year = y-1900;
    local.tm_isdst = -1;

    return mktime(&local);
}

static int datetime_from_time_t(time_t t)
{
    struct tm *local;
    int ans;

    /* Round to nearest minute */
    int min_offset = ((long) t) % 60;
    if (min_offset >= 30) {
        t += (60 - min_offset);
    } else {
        t -= min_offset;
    }

    local = localtime(&t);

    ans = DSE(local->tm_year + 1900, local->tm_mon, local->tm_mday) * MINUTES_PER_DAY;
    ans += local->tm_hour * 60;
    ans += local->tm_min;
    return ans;
}

/* 3-point interpolation */
static double interpolate(double f0, double f1, double f2, double p)
{
    double a = f1-f0;
    double b = f2-f1-a;
    return f0 + p * (2*a + b * (2*p - 1));
}

/* Moon position using fundamental arguments
   Van Flandern & Pulkkinen, 1979) */
void moon_position(double dayOffset, double *ra, double *declination, double *distance)
{
    double l = 0.606434 + 0.03660110129 * dayOffset;
    double m = 0.374897 + 0.03629164709 * dayOffset;
    double f = 0.259091 + 0.03674819520 * dayOffset;
    double d = 0.827362 + 0.03386319198 * dayOffset;
    double n = 0.347343 - 0.00014709391 * dayOffset;
    double g = 0.993126 + 0.00273777850 * dayOffset;

    l = 2 * PI * (l - floor(l));
    m = 2 * PI * (m - floor(m));
    f = 2 * PI * (f - floor(f));
    d = 2 * PI * (d - floor(d));
    n = 2 * PI * (n - floor(n));
    g = 2 * PI * (g - floor(g));

    double v, u, w;
    v = 0.39558 * sin(f + n)
        + 0.08200 * sin(f)
        + 0.03257 * sin(m - f - n)
        + 0.01092 * sin(m + f + n)
        + 0.00666 * sin(m - f)
        - 0.00644 * sin(m + f - 2*d + n)
        - 0.00331 * sin(f - 2*d + n)
        - 0.00304 * sin(f - 2*d)
        - 0.00240 * sin(m - f - 2*d - n)
        + 0.00226 * sin(m + f)
        - 0.00108 * sin(m + f - 2*d)
        - 0.00079 * sin(f - n)
        + 0.00078 * sin(f + 2*d + n);
    u = 1
        - 0.10828 * cos(m)
        - 0.01880 * cos(m - 2*d)
        - 0.01479 * cos(2*d)
        + 0.00181 * cos(2*m - 2*d)
        - 0.00147 * cos(2*m)
        - 0.00105 * cos(2*d - g)
        - 0.00075 * cos(m - 2*d + g);
    w = 0.10478 * sin(m)
        - 0.04105 * sin(2*f + 2*n)
        - 0.02130 * sin(m - 2*d)
        - 0.01779 * sin(2*f + n)
        + 0.01774 * sin(n)
        + 0.00987 * sin(2*d)
        - 0.00338 * sin(m - 2*f - 2*n)
        - 0.00309 * sin(g)
        - 0.00190 * sin(2*f)
        - 0.00144 * sin(m + n)
        - 0.00144 * sin(m - 2*f - n)
        - 0.00113 * sin(m + 2*f + 2*n)
        - 0.00094 * sin(m - 2*d + g)
        - 0.00092 * sin(2*m - 2*d);

    double s;
    s = w / sqrt(u - v*v);
    *ra = l + atan(s / sqrt(1 - s*s));		      /* Right ascension */

    s = v / sqrt(u);
    *declination = atan(s / sqrt(1 - s*s));	      /* Declination */
    *distance = 60.40974 * sqrt(u);		      /* Distance */
}

/* Search for moonrise / moonset events during an hour */
static void test_moon_event(int k, double offset_days, struct MoonInfo *moon_info,
                            double latitude, double longitude,
                            double const ra[], double declination[], double const distance[])
{
    double ha[3], VHz[3];
    double lSideTime;

    /* Get (local_sidereal_time - MR_WINDOW / 2) hours in radians. */
    lSideTime = local_sidereal_time(offset_days, longitude) * PI / 180.0;

    /* Calculate hour angle */
    ha[0] = lSideTime - ra[0] + k*K1;
    ha[2] = lSideTime - ra[2] + k*K1 + K1;

    /* Hour Angle and declination at half hour. */
    ha[1]  = (ha[2] + ha[0])/2;
    declination[1] = (declination[2] + declination[0])/2;

    double s = sin((PI / 180) * latitude);
    double c = cos((PI / 180) * latitude);

    /* refraction + semidiameter at horizon + distance correction */
    double z = cos((PI / 180) * (90.567 - 41.685 / distance[0]));

    VHz[0] = s * sin(declination[0]) + c * cos(declination[0]) * cos(ha[0]) - z;
    VHz[2] = s * sin(declination[2]) + c * cos(declination[2]) * cos(ha[2]) - z;

    if (signbit(VHz[0]) == signbit(VHz[2]))
        goto noevent;			    /* No event this hour. */

    VHz[1] = s * sin(declination[1]) + c * cos(declination[1]) * cos(ha[1]) - z;

    double a, b, d, e, time;
    a = 2 * VHz[2] - 4 * VHz[1] + 2 * VHz[0];
    b = 4 * VHz[1] - 3 * VHz[0] - VHz[2];
    d = b * b - 4 * a * VHz[0];

    if (d < 0)
        goto noevent;			    /* No event this hour. */

    d = sqrt(d);
    e = (-b + d) / (2 * a);
    if ((e < 0) || (e > 1))
        e = (-b - d) / (2 * a);
    time = k + e + 1 / 120;	    /* Time since k=0 of event (in hours). */


    /* The time we started searching + the time from the start of the
       search to the event is the time of the event.  Add (time since
       k=0) - window/2 hours. */
    time_t eventTime;
    eventTime = moon_info->queryTime + (time - MR_WINDOW / 2) *60 *60;

    double hz, nz, dz, az;
    hz = ha[0] + e * (ha[2] - ha[0]);    /* Azimuth of the moon at the event. */
    nz = -cos(declination[1]) * sin(hz);
    dz = c * sin(declination[1]) - s * cos(declination[1]) * cos(hz);
    az = atan2(nz, dz) * (180 / PI);
    if (az < 0) {
        az += 360;
    }

    /* If there is no previously recorded event of this type, save this event.

     If this event is previous to queryTime, and is the nearest event
     to queryTime of events of its type previous to queryType, save
     this event, replacing the previously recorded event of its type.
     Events subsequent to queryTime are treated similarly, although
     since events are tested in chronological order no replacements
     will occur as successive events will be further from queryTime.

     If this event is subsequent to queryTime and there is an event of
     its type previous to queryTime, then there is an event of the
     other type between the two events of this event's type.  If the
     event of the other type is previous to queryTime, then it is the
     nearest event to queryTime that is previous to queryTime.  In
     this case save the current event, replacing the previously
     recorded event of its type.  Otherwise discard the current
     event. */

    if ((VHz[0] < 0) && (VHz[2] > 0)) {
        if (!moon_info->hasRise ||
            ((moon_info->riseTime < moon_info->queryTime) == (eventTime < moon_info->queryTime) &&
             labs(moon_info->riseTime - moon_info->queryTime) > labs(eventTime - moon_info->queryTime)) ||
            ((moon_info->riseTime < moon_info->queryTime) != (eventTime < moon_info->queryTime) &&
             (moon_info->hasSet && 
              (moon_info->riseTime < moon_info->queryTime) == (moon_info->setTime < moon_info->queryTime)))) {
            moon_info->riseTime = eventTime;
            moon_info->riseAz = az;
            moon_info->hasRise = 1;
        }
    }
    if ((VHz[0] > 0) && (VHz[2] < 0)) {
        if (!moon_info->hasSet ||
            ((moon_info->setTime < moon_info->queryTime) == (eventTime < moon_info->queryTime) &&
             labs(moon_info->setTime - moon_info->queryTime) > labs(eventTime - moon_info->queryTime)) ||
            ((moon_info->setTime < moon_info->queryTime) != (eventTime < moon_info->queryTime) &&
             (moon_info->hasRise && 
              (moon_info->setTime < moon_info->queryTime) == (moon_info->riseTime < moon_info->queryTime)))) {
            moon_info->setTime = eventTime;
            moon_info->setAz = az;
            moon_info->hasSet = 1;
        }
    }

  noevent:
    /* There are obscure cases in the polar regions that require extra logic. */
    if (!moon_info->hasRise && !moon_info->hasSet)
        moon_info->isVisible = !signbit(VHz[2]);
    else if (moon_info->hasRise && !moon_info->hasSet)
        moon_info->isVisible = (moon_info->queryTime > moon_info->riseTime);
    else if (!moon_info->hasRise && moon_info->hasSet)
        moon_info->isVisible = (moon_info->queryTime < moon_info->setTime);
    else
        moon_info->isVisible = ((moon_info->riseTime < moon_info->setTime && moon_info->riseTime < moon_info->queryTime && moon_info->setTime > moon_info->queryTime) ||
                     (moon_info->riseTime > moon_info->setTime && (moon_info->riseTime < moon_info->queryTime || moon_info->setTime > moon_info->queryTime)));

    return;
}

static void
calculate_moonrise_moonset(double latitude, double longitude, time_t t,
                           struct MoonInfo *mi)
{
    double ra[3], declination[3], distance[3];
    double offset_days;
    int i;

    init_moon_info(mi);

    mi->queryTime = t;

    /* Days since Jan 1, 2000 12:00UTC */
    offset_days = julian_from_time_t(t) - 2451545L;

    offset_days -= (double) MR_WINDOW / (2.0 * 24.0);

    for (i=0; i<3; i++) {
        moon_position(offset_days + i * ((double) MR_WINDOW / (2.0 * 24.0)), &ra[i], &declination[i], &distance[i]);
    }

    if (ra[1] <= ra[0]) {
        ra[1] += 2*PI;
    }
    if (ra[2] <= ra[1]) {
        ra[2] += 2*PI;
    }

    double window_ra[3], window_declination[3], window_distance[3];

    window_ra[0] = ra[0];
    window_declination[0] = declination[0];
    window_distance[0] = distance[0];

    for (int k=0; k < MR_WINDOW; k++) {
        double ph = (double) (k+1) / (double) MR_WINDOW;
        window_ra[2] = interpolate(ra[0], ra[1], ra[2], ph);
        window_declination[2] = interpolate(declination[0], declination[1], declination[2], ph);
        window_distance[2] = interpolate(distance[0], distance[1], distance[2], ph);
        test_moon_event(k, offset_days, mi, latitude, longitude, window_ra, window_declination, window_distance);

        /* Step to next interval */
        window_ra[0] = window_ra[2];
        window_declination[0] = window_declination[2];
        window_distance[0] = window_distance[2];
    }
}

/* Get next moonrise or moonset in minutes after midnight of BASEYR
   starting from given DSE.
   Returns 0 if no moonrise could be computed
   If want_angle is true, then returns the azimuth of the event rather
   than the time of the event */

#define ME_SEARCH_DAYS 180
static int GetMoonevent(int dse, int is_rise, int want_angle)
{
    int i;
    int angle;
    struct MoonInfo mi;
    time_t t = time_t_from_dse(dse);
    for (i=0; i<ME_SEARCH_DAYS; i++) {
        calculate_moonrise_moonset(Latitude, Longitude, t + i * 86400, &mi);
        if (is_rise) {
            if (mi.hasRise && mi.riseTime >= t) {
                if (want_angle) {
                    angle = (int) (mi.riseAz + 0.5);
                    return angle;
                } else {
                    return datetime_from_time_t(mi.riseTime);
                }
            }
        } else {
            if (mi.hasSet && mi.setTime >= t) {
                if (want_angle) {
                    angle = (int) (mi.setAz + 0.5);
                    return angle;
                } else {
                    return datetime_from_time_t(mi.setTime);
                }
            }
        }
    }
    if (want_angle) {
        return -1;
    } else {
        return 0;
    }
}

int GetMoonrise(int dse)
{
    return GetMoonevent(dse, 1, 0);
}
int GetMoonset(int dse)
{
    return GetMoonevent(dse, 0, 0);
}

int GetMoonrise_angle(int dse)
{
    return GetMoonevent(dse, 1, 1);
}
int GetMoonset_angle(int dse)
{
    return GetMoonevent(dse, 0, 1);
}
