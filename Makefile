# insitu - in place file replacement for Unix pipelines
# See LICENSE file for copyright and license details.

include config.mk

SRC = insitu.c
OBJ = ${SRC:.c=.o}

all: insitu

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

insitu: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ insitu.o ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f insitu ${OBJ} insitu-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p insitu-${VERSION}
	@cp -R LICENSE Makefile config.mk README \
		insitu.1 ${SRC} insitu-${VERSION}
	@tar -cf insitu-${VERSION}.tar insitu-${VERSION}
	@gzip insitu-${VERSION}.tar
	@rm -rf insitu-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f insitu ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/insitu
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < insitu.1 > ${DESTDIR}${MANPREFIX}/man1/insitu.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/insitu.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/insitu
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/insitu.1

.PHONY: all options clean dist install uninstall
