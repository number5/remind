# REMIND

Remind is a full-featured calendar/alarm program.  Copying policy is
in the file "COPYRIGHT" included with the source; Remind is licensed under
the GNU General Public License, Vesion 2.

## Prerequisites:

remind and rem2ps have no prerequisites beyond the standard C library and
the standard math library.

rem2html requires the JSON::MaybeXS Perl module and rem2pdf
requires the JSON::MaybeXS, Pango and Cairo Perl modules.

- On Debian-like systems, these prerequisites may be installed with:

    `apt install libjson-maybexs-perl libpango-perl libcairo-perl`

- On RPM-based systems, you need `perl-Pango`, `perl-Cairo` and
  `perl-JSON-MaybeXS`

- On Gentoo, you need `dev-perl/Pango`, `dev-perl/Cairo` and
  `dev-perl/JSON-MaybeXS`.

- On Arch linux, you need `pango-perl`, `cairo-perl` and `perl-json-maybexs`

TkRemind requires Tcl/Tk and the tcllib library.

- On Debian-like systems, install with:

    `apt install tcl tk tcllib`

- On RPM-based systems, you need `tcl`, `tk` and `tcllib`

- On Arch Linux, you need `tk` and `tcllib`.  The latter is available at
  https://aur.archlinux.org/packages/tcllib

If the little arrows for "Previous Month" and "Next Month" do not display
correctly in TkRemind, you may need to install the Noto Fonts.  Install
all of your distribution's Noto Font-related packages.

- On Debian-like systems, install with:

    `apt install fonts-noto-core fonts-noto-color-emoji fonts-noto-extra fonts-noto-ui-core fonts-noto-ui-extra`

## Installation

Remind can be installed with the usual:

`./configure && make && make test && sudo make install`

You can edit custom.h to configure some aspects of Remind.  Or, if
you have Tcl/Tk installed, you can use the graphical build tool to
edit custom.h on your behalf:

`wish ./build.tk`

## Usage

Remind is a large and complex program.  You can read the full manual page
with:

`man remind`

after installation.  However, the man page is long and detailed and is
more of a reference than an introduction.  You can get an overview
with a [slide deck](https://dianne.skoll.ca/projects/remind/download/remind-oclug.pdf)
I made a while back.  There's also a (long) [YouTube video](https://www.youtube.com/watch?v=0SNgvsDvx7M) that serves as an
introduction to Remind.

## A Note about AI

1. No part of Remind was written using AI of any type.<br><br>
I certify that all of the C, Perl and Tcl code in Remind was written
by a human being.  I certify that all code in `.rem` files other than
ones under `include/holidays` was written by a human being.  The code
under `include/holidays` was derived from the Python "holidays" library
and I have no direct knowledge of the provenance of that library,
though I suspect it's entirely or almost entirely human-written.

2. No AI-generated patches or other sorts of contributions to Remind
will be accepted.

3. Remind's source code may not be used to train an AI model,
including an LLM model, unless all of the output of said model is
released under the GNU General Public License, version 2.  If you use
any of Remind's source code to train your model, then anything that
the model produces is a derived product of Remind and must be licensed
under the same terms as Remind.

---

Contact info: dianne@skoll.ca

Home page:    [https://dianne.skoll.ca/projects/remind/](https://dianne.skoll.ca/projects/remind/)

