#!/bin/sh

for i in ../include/lang/??.rem ; do
    echo "Testing lang file: $i"
    ../src/remind -r -q "-ii=\"$i\"" ../tests/tstlang.rem
done
