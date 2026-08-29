#!/bin/sh

if test "$@" = "" ; then
    echo "Usage: $0 lang_code"
    exit 1
fi

LANG=$1
MYDIR=`dirname $0`
cd $MYDIR
test -f ../include/lang/$LANG.rem
if test $? != 0 ; then
    echo "include/lang/$LANG.rem not found"
    exit 1
fi

echo "Testing lang file: $LANG"
../src/remind -r -q "-ii=\"../include/lang/$LANG.rem\"" ../tests/tstlang.rem 2026-08-29@16:35


