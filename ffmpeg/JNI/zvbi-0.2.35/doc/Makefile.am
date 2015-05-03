## Process this file with automake to produce Makefile.in

EXTRA_DIST = \
	Doxyfile \
	zvbi_525.gif \
	zvbi_525.ps \
	zvbi_625.gif \
	zvbi_625.ps

all-local: html-build.stamp

html-build.stamp: Doxyfile
if HAVE_DOXYGEN
	doxygen Doxyfile
endif
	touch html-build.stamp

clean-local:
	rm -rf html html-build.stamp
