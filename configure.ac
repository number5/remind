dnl Process this file with autoconf to produce a configure script.

AC_INIT(remind, 05.05.00, , , https://dianne.skoll.ca/projects/remind/)
AC_CONFIG_SRCDIR([src/queue.c])

cat <<'EOF'

**********************
*                    *
* Configuring REMIND *
*                    *
**********************

EOF

AC_CONFIG_HEADERS([src/config.h])

AC_ARG_ENABLE(perl-build-artifacts,
[  --disable-perl-build-artifacts
                          Disable perllocal.pod and .packlist generation], ac_cv_perlartifacts=$enableval, ac_cv_perlartifacts=yes)

AH_BOTTOM([#include <custom.h>])

dnl Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PATH_PROG([PERL], [perl])

dnl Checks for libraries.
AC_CHECK_LIB(m, sqrt)
AC_CHECK_HEADERS_ONCE([sys/time.h stdint.h])

dnl Integer sizes
AC_CHECK_SIZEOF(unsigned int)
AC_CHECK_SIZEOF(unsigned long)
AC_CHECK_SIZEOF(time_t)

dnl Checks for header files.
AC_CHECK_HEADERS(strings.h sys/types.h glob.h wctype.h locale.h langinfo.h sys/inotify.h)

dnl Checks for typedefs, structures, and compiler characteristics.
AC_STRUCT_TM

if test "$GCC" = yes; then
	CFLAGS="$CFLAGS -Wall -Wextra -Wstrict-prototypes"
        # Check for link-time optimization support
        f=-flto=auto
        AC_MSG_CHECKING([whether $CC supports $f])
        if $CC -Werror -E $f - < /dev/null > /dev/null 2>&1 ; then
            AC_MSG_RESULT([yes])
            CFLAGS="$CFLAGS $f"
            f=-ffat-lto-objects
            AC_MSG_CHECKING([whether $CC supports $f])
            if $CC -Werror -E $f - < /dev/null > /dev/null 2>&1 ; then
                AC_MSG_RESULT([yes])
                CFLAGS="$CFLAGS $f"
            else
                AC_MSG_RESULT([no])
            fi
        else
            AC_MSG_RESULT([no])
        fi
fi

dnl If sizeof(time_t) is 4, try to get 64-bit time_t
if test "$ac_cv_sizeof_time_t" = "4" ; then
    AC_MSG_NOTICE([time_t is 32-bits on this system; attempting to use 64-bit time_t])
    CFLAGS="$CFLAGS -D_TIME_BITS=64 -D_FILE_OFFSET_BITS=64"
fi

if test "$ac_cv_perlartifacts" = "yes" ; then
    PERLARTIFACTS=
else
    PERLARTIFACTS='NO_PACKLIST=1 NO_PERLLOCAL=1'
fi

RELEASE_DATE=`grep '[[0-9]][[0-9]][[0-9]][[0-9]]-[[0-9]][[0-9]]-[[0-9]][[0-9]]' docs/WHATSNEW  | head -n 1 | awk '{print $NF}'`

# Sanity-check release date
echo "$RELEASE_DATE" | grep '^....-..-..$' > /dev/null 2>&1
if test "$?" != 0 ; then
    echo "*** COULD NOT DETERMINE RELEASE DATE: docs/WHATSNEW is incorrect!"
    exit 1
fi
AC_CHECK_FUNCS(strdup strcasecmp strncasecmp setenv unsetenv glob mbstowcs setlocale initgroups inotify_init1)

VERSION=$PACKAGE_VERSION
CONFIG_CMD="$0$ac_configure_args_raw"
CONFIG_CMD=`echo "$CONFIG_CMD" | sed -e 's/"/\\\\"/g'`
AC_DEFINE_UNQUOTED([CONFIG_CMD], ["$CONFIG_CMD"], [Configuration command used to build Remind])
AC_SUBST(CONFIG_CMD)
AC_SUBST(VERSION)
AC_SUBST(PERL)
AC_SUBST(PERLARTIFACTS)
AC_SUBST(RELEASE_DATE)
AC_CONFIG_FILES([src/Makefile www/Makefile src/version.h rem2html/Makefile rem2html/rem2html rem2pdf/Makefile.PL rem2pdf/Makefile.top rem2pdf/bin/rem2pdf man/rem.1 man/rem2ps.1 man/remind.1 man/tkremind.1 scripts/tkremind])
AC_OUTPUT
chmod a+x rem2pdf/bin/rem2pdf
chmod a+x scripts/tkremind
