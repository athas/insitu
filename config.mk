# insitu version
VERSION = 1.0

# Customize below to fit your system

# paths
PREFIX = ~/.local/
MANPREFIX = ${PREFIX}/share/man

# includes and libs
INCS = -I.
LIBS = -L/usr/lib -lc

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\"
CFLAGS = -pedantic -Wall -Wextra -Os ${INCS} ${CPPFLAGS}
LDFLAGS = -g ${LIBS}

# compiler and linker
CC = cc
