# If Windows, then assume the compiler is `gcc` for the
# MinGW environment. I can't figure out how to tell if it's
# actually MingGW. FIXME TODO
ifeq ($(OS),Windows_NT)
    CC = gcc
endif

# Try to figure out the default compiler. I dont know the best
# way to do this with `gmake`. If you have better ideas, please
# submit a pull request on github.
ifeq ($(CC),)
ifneq (, $(shell which clang))
CC = clang
else ifneq (, $(shell which gcc))
CC = gcc
else
CC = cc
endif
endif

PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
SYS := $(shell $(CC) -dumpmachine)
INSTALL_DATA := -pDm755

# LINUX
# The automated regression tests run on Linux, so this is the one
# environment where things likely will work -- as well as anything
# works on the bajillion of different Linux environments
ifneq (, $(findstring linux, $(SYS)))
ifneq (, $(findstring musl, $(SYS)))
LIBS = 
else
LIBS = -lm -lrt -ldl -lpthread
endif
INCLUDES =
FLAGS2 = 
endif

# MAC OS X
# I occassionally develope code on Mac OS X, but it's not part of
# my regularly regression-test environment. That means at any point
# in time, something might be minorly broken in Mac OS X.
ifneq (, $(findstring darwin, $(SYS)))
LIBS = -lm 
INCLUDES = -I.
FLAGS2 = 
INSTALL_DATA = -pm755
endif

# MinGW on Windows
# I develope on Visual Studio 2010, so that's the Windows environment
# that'll work. However, 'git' on Windows runs under MingGW, so one
# day I acccidentally typed 'make' instead of 'git, and felt compelled
# to then fix all the errors, so this kinda works now. It's not the
# intended environment, so it make break in the future.
ifneq (, $(findstring mingw, $(SYS)))
INCLUDES = -Ivs10/include
LIBS = -L vs10/lib -lIPHLPAPI -lWs2_32
#FLAGS2 = -march=i686
endif

# Cygwin
# I hate Cygwin, use Visual Studio or MingGW instead. I just put this
# second here for completeness, or in case I gate tired of hitting my
# head with a hammer and want to feel a different sort of pain.
ifneq (, $(findstring cygwin, $(SYS)))
INCLUDES = -I.
LIBS = 
FLAGS2 = 
endif

# OpenBSD
ifneq (, $(findstring openbsd, $(SYS)))
LIBS = -lm -lpthread
INCLUDES = -I.
FLAGS2 = 
endif

# FreeBSD
ifneq (, $(findstring freebsd, $(SYS)))
LIBS = -lm -lpthread
INCLUDES = -I.
FLAGS2 =
endif

# NetBSD
ifneq (, $(findstring netbsd, $(SYS)))
LIBS = -lm -lpthread
INCLUDES = -I.
FLAGS2 =
endif


DEFINES = 
CFLAGS = -g -ggdb $(FLAGS2) $(INCLUDES) $(DEFINES) -Wall -O2
.SUFFIXES: .c .cpp

all: bin/xtate


# tmp/main-conf.o: src/xconf.c src/*.h
# 	$(CC) $(CFLAGS) -c $< -o $@


# just compile everything in the 'src' directory. Using this technique
# means that include file dependencies are broken, so sometimes when
# the program crashes unexpectedly, 'make clean' then 'make' fixes the
# problem that a .h file was out of date
tmp/%.o: \
	src/%.c \
	src/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/crypto/%.c \
	src/crypto/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/massip/%.c \
	src/massip/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/pixie/%.c \
	src/pixie/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/proto/%.c \
	src/proto/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/rawsock/%.c \
	src/rawsock/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/scripting/%.c \
	src/scripting/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/stack/%.c \
	src/stack/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/stub/%.c \
	src/stub/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/templ/%.c \
	src/templ/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/util/%.c \
	src/util/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/smack/%.c \
	src/smack/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/nmap-service/%.c \
	src/nmap-service/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/probe-modules/%.c \
	src/probe-modules/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/probe-modules/lzr-probes/%.c \
	src/probe-modules/lzr-probes/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/scan-modules/%.c \
	src/scan-modules/*.h
	$(CC) $(CFLAGS) -c $< -o $@

tmp/%.o: \
	src/output/%.c \
	src/output/*.h
	$(CC) $(CFLAGS) -c $< -o $@


SRC = $(sort $(wildcard \
	src/*.c \
	src/crypto/*.c \
	src/massip/*.c \
	src/pixie/*.c \
	src/proto/*.c \
	src/rawsock/*.c \
	src/scripting/*.c \
	src/stack/*.c \
	src/stub/*.c \
	src/templ/*.c \
	src/util/*.c \
	src/smack/*.c \
	src/nmap-service/*.c \
	src/probe-modules/*.c \
	src/probe-modules/lzr-probes/*.c \
	src/scan-modules/*.c \
	src/output/*.c \
	))
OBJ = $(addprefix tmp/, $(notdir $(addsuffix .o, $(basename $(SRC))))) 


bin/xtate: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)

clean:
	rm -f tmp/*.o
	rm -f bin/xtate

regress: bin/xtate
	bin/xtate --selftest

test: regress

install: bin/xtate
	install $(INSTALL_DATA) bin/xtate $(DESTDIR)$(BINDIR)/xtate
	
default: bin/xtate
