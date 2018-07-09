# (c) 2018 Jan Stary <hans@stare.cz>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

VERSION = 1.0
TARBALL = rtp-$(VERSION).tar.gz

BINS =	rtp
MAN1 =	rtp.1	

OBJS =	rtp.o		\
	format-dump.o	\
	format-rtp.o

SRCS =	rtp.c		\
	format-dump.c	\
	format-dump.h	\
	format-rtp.c	\
	format-rtp.h

HAVE_SRCS =	have-err.c have-progname.c have-strtonum.c have-bigendian.c
COMPAT_SRCS =	compat-err.c compat-progname.c compat-strtonum.c
COMPAT_OBJS =	compat-err.o compat-progname.o compat-strtonum.o
OBJS +=		$(COMPAT_OBJS)

DISTFILES = \
	LICENSE			\
	Makefile		\
	Makefile.depend		\
	configure		\
	configure.local.example	\
	$(MAN1)			\
	$(SRCS)			\
	$(HAVE_SRCS)		\
	$(COMPAT_SRCS)		\
	session.rtp

include Makefile.local

all: $(BINS) $(MAN1) Makefile.local

.PHONY: install clean distclean depend

include Makefile.depend

clean:
	rm -f $(TARBALL) $(BINS) $(OBJS)
	rm -rf *.dSYM *.core *~ .*~
	rm -f session.{raw,txt}
	rm -rf rtp-$(VERSION)

distclean: clean
	rm -f Makefile.local config.h config.h.old config.log config.log.old

install: all
	install -d $(BINDIR)      && install -m 0755 $(BINS) $(BINDIR)
	install -d $(MANDIR)/man1 && install -m 0444 $(MAN1) $(MANDIR)/man1

uninstall:
	cd $(BINDIR)      && rm $(PROG)
	cd $(MANDIR)/man1 && rm $(MAN1)

lint: $(MAN1)
	mandoc -Tlint -Wstyle $(MAN1)

test: $(BINS)
	./rtp -v session.rtp session.raw

Makefile.local config.h: configure $(HAVESRCS)
	@echo "$@ is out of date; please run ./configure"
	@exit 1

rtp: $(OBJS)
	$(CC) $(CFLAGS) -o rtp $(OBJS) $(LDADD)

# --- maintainer targets ---

depend: config.h
	mkdep -f depend $(CFLAGS) $(SRCS)
	perl -e 'undef $$/; $$_ = <>; s|/usr/include/\S+||g; \
		s|\\\n||g; s|  +| |g; s| $$||mg; print;' \
		depend > _depend
	mv _depend depend

dist: $(TARBALL)

$(TARBALL): $(DISTFILES)
	rm -rf .dist
	mkdir -p .dist/rtp-$(VERSION)/
	$(INSTALL) -m 0644 $(DISTFILES) .dist/rtp-$(VERSION)/
	( cd .dist/rtp-$(VERSION) && chmod 755 configure $(MULT) )
	( cd .dist && tar czf ../$@ rtp-$(VERSION) )
	rm -rf .dist/

distcheck: dist
	rm -rf rtp-$(VERSION) && tar xzf $(TARBALL)
	( cd rtp-$(VERSION) && ./configure && make all )

.SUFFIXES: .c .o

.c.o:
	$(CC) $(CFLAGS) -c $<
