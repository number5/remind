# Top-level Makefile for Remind.
# SPDX-License-Identifier: GPL-2.0-only

all: src/Makefile
	@echo ""
	@echo "*******************"
	@echo "*                 *"
	@echo "* Building REMIND *"
	@echo "*                 *"
	@echo "*******************"
	@echo ""
	@cd src && $(MAKE) all LANGDEF=$(LANGDEF)
	@$(MAKE) -C rem2pdf -f Makefile.top

uninstall-script:
	@echo "" >&2
	@echo "*****************************" >&2
	@echo "*                           *" >&2
	@echo "* Creating Uninstall Script *" >&2
	@echo "*                           *" >&2
	@echo "*****************************" >&2
	@echo "" >&2

	@echo "#!/bin/sh"
	@echo "echo 'This script will uninstall Remind'"
	@echo "echo 'Enter y to uninstall Remind or anything else to abort'"
	@echo "read ans"
	@echo 'if test "$$ans" != "y" ; then'
	@echo "    echo 'NOT uninstalling Remind'"
	@echo "    exit 0"
	@echo "fi"
	@echo "echo 'Uninstalling Remind...'"
	-@rm -rf `pwd`/.uninstall-dir > /dev/null 2>&1
	@mkdir `pwd`/.uninstall-dir >&2
	@$(MAKE) install DESTDIR=`pwd`/.uninstall-dir >&2
	@cd `pwd`/.uninstall-dir && find . -type f | while read x ; do x=`echo $$x | sed -e 's|^\./|/|'`; echo "rm -f $$x"; done;
	@echo "echo 'Done'"
	-@rm -rf `pwd`/.uninstall-dir > /dev/null 2>&1


install:
	@echo ""
	@echo "**********************************"
	@echo "*                                *"
	@echo "* Installing REMIND (unstripped) *"
	@echo "*                                *"
	@echo "**********************************"
	@echo ""
	@$(MAKE) -C src install DESTDIR=$(DESTDIR)
	@$(MAKE) -C rem2html install DESTDIR=$(DESTDIR)
	@$(MAKE) -C rem2pdf -f Makefile.top install INSTALL_BASE=$(INSTALL_BASE) DESTDIR=$(DESTDIR)
clean:
	-find . -name '*~' -exec rm {} \;
	-$(MAKE) -C src clean
	-$(MAKE) -C rem2pdf clean

install-stripped:
	@echo ""
	@echo "********************************"
	@echo "*                              *"
	@echo "* Installing REMIND (stripped) *"
	@echo "*                              *"
	@echo "**********************************"
	@echo ""
	@$(MAKE) -C src install-stripped
	@$(MAKE) -C rem2html install
	@$(MAKE) -C rem2pdf -f Makefile.top install INSTALL_BASE=$(INSTALL_BASE)

test: test-basic test-tz

test-tz:
	@$(MAKE) -C src -s all
	@$(MAKE) -C src -s test-tz

test-basic:
	@$(MAKE) -C src -s all
	@$(MAKE) -C src -s test-basic

cppcheck:
	@$(MAKE) -C src cppcheck

distclean: clean
	-rm -f config.cache config.log config.status src/Makefile src/version.h src/config.h tests/test.out tests/tz.out www/Makefile rem2pdf/Makefile.top rem2pdf/Makefile.old rem2pdf/Makefile rem2pdf/Makefile.PL rem2pdf/bin/rem2pdf rem2html/rem2html
	-rm -f man/rem.1 man/rem2ps.1 man/remind.1 man/tkremind.1 scripts/tkremind
	-rm -rf autom4te.cache rem2html/Makefile rem2html/rem2html.1

src/Makefile: src/Makefile.in
	./configure

# DO NOT DELETE

