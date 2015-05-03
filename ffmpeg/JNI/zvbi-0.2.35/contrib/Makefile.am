## Process this file with automake to produce Makefile.in

bin_PROGRAMS = zvbi-ntsc-cc 

zvbi_ntsc_cc_SOURCES = ntsc-cc.c

noinst_PROGRAMS = x11font

man_MANS = zvbi-ntsc-cc.1

# These programs work only with Linux DVB drivers.
if ENABLE_DVB

bin_PROGRAMS += zvbi-atsc-cc

# noinst_PROGRAMS += zvbi-dvbsubs

zvbi_atsc_cc_SOURCES = atsc-cc.c
man_MANS += zvbi-atsc-cc.1

# zvbi_dvbsubs_SOURCES = dvbsubs.c dvbsubs.h

endif

EXTRA_DIST = \
	$(man_MANS) \
	atsc-cc.c \
	dvbsubs.c \
	dvbsubs.h \
	zvbi-atsc-cc.1 \
	README

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	$(X_CFLAGS) \
	-D_REENTRANT \
	-D_GNU_SOURCE

# For easier debugging.
if BUILD_STATIC_LIB
LDADD = $(top_builddir)/src/.libs/libzvbi.a
else
LDADD = $(top_builddir)/src/libzvbi.la
endif

LDADD += \
	$(LIBS) \
	$(X_LIBS)
