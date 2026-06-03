CC      ?= cc
CFLAGS  += -Wall -Wextra -O2 -std=c11
LDFLAGS ?=
TARGET   = gfetch
SRC      = src/gfetch.c

STATIC_1 = -static
LDFLAGS += ${STATIC_${STATIC}}

PREFIX  ?= /usr/local
BINDIR   = ${PREFIX}/bin

all: ${TARGET}

${TARGET}: ${SRC}
	${CC} ${CFLAGS} -o $@ $< ${LDFLAGS}

install: ${TARGET}
	install -Dm755 ${TARGET} ${DESTDIR}${BINDIR}/${TARGET}

uninstall:
	rm -f ${DESTDIR}${BINDIR}/${TARGET}

clean:
	rm -f ${TARGET}

.PHONY: all install uninstall clean
