CC=cc
LD=cc
O=o

<| if [ -d /sys/include ]; then echo "</$objtype/mkfile"; fi

TARGET=gfetch
SRC=src/gfetch.c
OFILES=src/gfetch.$O

PREFIX=/usr/local
BINDIR=$PREFIX/bin

all:V: $TARGET

$TARGET: $OFILES
	$LD $LDFLAGS -o $target $prereq

%.$O: %.c
	$CC $CFLAGS -c $stem.c -o $target

install:V: $TARGET
	mkdir -p $DESTDIR$BINDIR
	cp $TARGET $DESTDIR$BINDIR/$TARGET

uninstall:V:
	rm -f $DESTDIR$BINDIR/$TARGET

clean:V:
	rm -f $OFILES $TARGET

