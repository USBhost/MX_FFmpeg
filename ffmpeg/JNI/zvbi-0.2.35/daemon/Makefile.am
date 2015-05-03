## Process this file with automake to produce Makefile.in

bin_PROGRAMS = zvbi-chains
zvbi_chains_SOURCES = chains.c

sbin_PROGRAMS = zvbid
zvbid_SOURCES = proxyd.c

man_MANS = \
	zvbid.1 \
	zvbi-chains.1

EXTRA_DIST = \
	$(man_MANS) \
	zvbid.init.in

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DLIBZVBI_CHAINS_PATH=\"libzvbi-chains.so\" \
	-D_REENTRANT \
	-D_GNU_SOURCE

LDADD = \
	$(top_builddir)/src/libzvbi.la \
	$(LIBS)
