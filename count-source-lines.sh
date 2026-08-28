#!/bin/sh

find . -type f \( -name '*.[ch]' -o -name '*.pm' -o -name 'rem2pdf.in' -o -name 'rem2html.in' -o -name 'tkremind.in' \) | xargs wc -l
