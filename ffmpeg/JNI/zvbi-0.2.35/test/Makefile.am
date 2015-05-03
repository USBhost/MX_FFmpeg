## Process this file with automake to produce Makefile.in

compile_tests = \
	ctest \
	cpptest

cpptest_SOURCES = cpptest.cc

if HAVE_GCC_C89_SUPPORT
compile_tests += ctest-c89 ctest-gnu89

ctest_c89_SOURCES = ctest.c
ctest_c89_CFLAGS = -pedantic-errors -std=c89

ctest_gnu89_SOURCES = ctest.c
ctest_gnu89_CFLAGS = -pedantic-errors -std=gnu89
endif

if HAVE_GCC_C94_SUPPORT
compile_tests += ctest-c94

ctest_c94_SOURCES = ctest.c
ctest_c94_CFLAGS = -pedantic-errors -std=iso9899:199409
endif

if HAVE_GCC_C99_SUPPORT
compile_tests += ctest-c99 ctest-gnu99

ctest_c99_SOURCES = ctest.c
ctest_c99_CFLAGS = -pedantic-errors -std=c99

ctest_gnu99_SOURCES = ctest.c
ctest_gnu99_CFLAGS = -pedantic-errors -std=gnu99
endif

if HAVE_GXX_CXX98_SUPPORT
compile_tests += cpptest-cxx98 cpptest-gnuxx98

cpptest_cxx98_SOURCES = cpptest.cc
cpptest_cxx98_CXXFLAGS = -pedantic-errors -std=c++98

cpptest_gnuxx98_SOURCES = cpptest.cc
cpptest_gnuxx98_CXXFLAGS = -pedantic-errors -std=gnu++98
endif

TESTS = \
	$(compile_tests) \
	exoptest \
	test-dvb_demux \
	test-dvb_mux \
	test-hamm \
	test-packet-830 \
	test-pdc \
	test-raw_decoder \
	test-unicode \
	test-vps

check_PROGRAMS = \
	$(compile_tests) \
	test-dvb_demux \
	test-dvb_mux \
	test-hamm \
	test-packet-830 \
	test-pdc \
	test-raw_decoder \
	test-vps

check_SCRIPTS = \
	exoptest \
	test-unicode

test_dvb_demux_SOURCES = \
	test-dvb_demux.cc \
	test-common.cc test-common.h

test_dvb_mux_SOURCES = \
	test-dvb_mux.cc \
	test-common.cc test-common.h

test_hamm_SOURCES = test-hamm.cc

test_packet_830_SOURCES = \
	test-packet-830.cc \
	test-pdc.h \
	test-common.cc test-common.h

test_pdc_SOURCES = \
	test-pdc.cc test-pdc.h \
	test-common.cc test-common.h

test_raw_decoder_SOURCES = \
	test-raw_decoder.cc \
	test-common.cc test-common.h

test_vps_SOURCES = \
	test-vps.cc \
	test-pdc.h \
	test-common.cc test-common.h

# exoptest: explist

# test-unicode: unicode

if ENABLE_PROXY
proxy_programs = proxy-test
else
proxy_programs =
endif

if HAVE_X
x_programs = caption osc
else
x_programs =
endif

noinst_PROGRAMS = \
	capture \
	date \
	decode \
	explist \
	export \
	glyph \
	sliced2pes \
	test-vps \
	ttxfilter \
	unicode \
	$(proxy_programs) \
	$(x_programs)

capture_SOURCES = \
	capture.c \
	sliced.c sliced.h

caption_SOURCES = \
	caption.c \
	sliced.c sliced.h

date_SOURCES = \
	date.c \
	sliced.c sliced.h

decode_SOURCES = \
	decode.c \
	sliced.c sliced.h

export_SOURCES = \
	export.c \
	sliced.c sliced.h

sliced2pes_SOURCES = \
	sliced2pes.c \
	sliced.c sliced.h

ttxfilter_SOURCES = \
	ttxfilter.c \
	sliced.c sliced.h

noinst_SCRIPTS = \
	uclist

EXTRA_DIST = \
	README \
	$(noinst_SCRIPTS) \
	$(check_SCRIPTS) \
	unicode-out-ref.txt

CLEANFILES = \
	unicode-out.txt

AM_CFLAGS = \
	-I$(top_srcdir) \
	$(X_CFLAGS) \
	-D_REENTRANT \
	-D_GNU_SOURCE

AM_CPPFLAGS = $(AM_CFLAGS)

# Libtool creates a wrapper script around the application until the
# library is installed, which is a major annoyance for debugging with
# gdb and valgrind. So we link these apps statically if possible.
if BUILD_STATIC_LIB
LDADD = $(top_builddir)/src/.libs/libzvbi.a
else
LDADD = $(top_builddir)/src/libzvbi.la
endif

LDADD += \
	$(LIBS) \
	$(X_LIBS)

unrename:
	for file in *.cc *.c *.h ; do \
	  case "$$file" in \
	  test-raw_decoder.cc|osc.c) continue ; ;; \
	  esac ; \
	  if grep -q -i vbi3_ $$file ; then \
	    sed 's/vbi3_/vbi_/g;s/VBI3_/VBI_/g' <$$file >tmp ; \
	    mv tmp $$file ; \
	  fi ; \
	done
