## Process this file with automake to produce Makefile.in

noinst_PROGRAMS = \
	network \
	pdc1 \
	pdc2 \
	rawout \
	wss

check_SCRIPTS = \
	pdc2-test1.sh

TESTS = \
	$(check_SCRIPTS)

EXTRA_DIST = \
	$(check_SCRIPTS)

INCLUDES = -I$(top_srcdir)/src

LDADD = $(top_builddir)/src/libzvbi.la \
	$(PTHREAD_LIB) -lm $(PNG_LIB)
