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
install:
	@echo ""
	@echo "**********************************"
	@echo "*                                *"
	@echo "* Installing REMIND (unstripped) *"
	@echo "*                                *"
	@echo "**********************************"
	@echo ""
	@$(MAKE) -C src install
	@$(MAKE) -C rem2html install
	@$(MAKE) -C rem2pdf -f Makefile.top install INSTALL_BASE=$(INSTALL_BASE)
clean:
	find . -name '*~' -exec rm {} \;
	-rm man/rem.1 man/rem2ps.1 man/remind.1 man/tkremind.1 scripts/tkremind
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

test:
	@$(MAKE) -C src -s test

distclean: clean
	rm -f config.cache config.log config.status src/Makefile src/config.h tests/test.out www/Makefile rem2pdf/Makefile.top rem2pdf/Makefile.old rem2pdf/Makefile rem2pdf/Makefile.PL rem2pdf/bin/rem2pdf rem2html/rem2html

src/Makefile: src/Makefile.in
	./configure

# DO NOT DELETE

