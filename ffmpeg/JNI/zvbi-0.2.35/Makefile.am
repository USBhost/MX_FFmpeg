## Process this file with automake to produce Makefile.in

if ENABLE_PROXY
proxy_dirs = daemon
else
proxy_dirs =
endif

EXTRA_DIST = \
	BUGS \
	COPYING \
	COPYING.LIB \
	config.rpath \
	depcomp \
	zvbi.spec.in

DISTCLEANFILES = site_def.h

# Attn order matters.
SUBDIRS = \
	m4 \
	src \
	$(proxy_dirs) \
	contrib \
	examples \
	test \
	po \
	doc

ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = zvbi-0.2.pc

dist-hook:
	chown -R 500:100 $(distdir)

fsf-addr-update:
	for dir in $(SUBDIRS); do \
	  cd $$dir ; \
	  for file in *.c *.h *.pl *.sh README ; do \
	    old='675 Mass Ave, Cambridge, MA 02139, USA' ; \
	    new='51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA' ; \
	    if grep -e "$$old" $$file ; then \
	      sed "s/$$old/$$new/" <$$file >$$file.tmp && mv $$file.tmp $$file ; \
	    fi
	  done ; \
	  cd - ; \
	done
