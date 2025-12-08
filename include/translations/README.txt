The files here contain additional translations for various files
distributed with Remind.

If you create a file in $SysInclude whose name is "myfile.rem", then
you can enable translation by putting this at the top of your
file:

#===================================================================
# Localize if we can
IF access($SysInclude + "/translations/" + _("LANGID") + "/myfile.rem", "r") >= 0
    SYSINCLUDE translations/[_("LANGID")]/myfile.rem
ENDIF
#===================================================================

Then you can localize your file by putting appropriate TRANSLATION directives
in the file $SysInclude/translations/<LC>/myfile.rem where <LC> is the
two-character language code.
