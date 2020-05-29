SQUIRTD_SRCS=squirtd.c squirtd_exec.c
SQUIRT_SRCS=squirt.c squirt_exec.c squirt_suck.c squirt_dir.c squirt_main.c util.c

CC=gcc
VBCC_CC=vc

AMIGA_GCC_PREFIX=/usr/local/amiga/bebbo
AMIGA_GCC=$(AMIGA_GCC_PREFIX)/bin/m68k-amigaos-gcc -I$(AMIGA_GCC_PREFIX)/m68k-amigaos/ndk-include/ -flto

DEBUG_CFLAGS=-g -fsanitize=address -fsanitize=undefined #-fanalyzer
WARNINGS=-Wno-error=format -Wno-format -Wall -Werror -Wall -Wpedantic -Wno-unknown-attributes -Wno-ignored-optimization-argument -Wno-unknown-pragmas  -Wmissing-field-initializers -Wfatal-errors -Wextra -Wshadow -Wuninitialized  -Wundef -Wbad-function-cast -Wparentheses -Wnull-dereference -pedantic-errors

LIBS=-liconv

CFLAGS=$(DEBUG_CFLAGS) -Os $(WARNINGS)
AMIGA_GCC_CFLAGS=-Os -fomit-frame-pointer -noixemul $(WARNINGS)
VBCC_CFLAGS=-O1 +aos68k -c99

SQUIRTD_AMIGA_OBJS=$(addprefix build/obj/amiga/, $(SQUIRTD_SRCS:.c=.o))
SQUIRTD_AMIGA_GCC_OBJS= $(addprefix build/obj/amiga.gcc/, $(SQUIRTD_SRCS:.c=.o))
SQUIRTD_OBJS=$(addprefix build/obj/, $(SQUIRTD_SRCS:.c=.o))
SQUIRT_OBJS=$(addprefix build/obj/, $(SQUIRT_SRCS:.c=.o))

CLIENT_APPS=build/squirt_exec build/squirt_suck build/squirt_dir build/squirt_backup build/squirt
SERVER_APPS=build/amiga/squirtd build/amiga/squirtd.vbcc

all: $(CLIENT_APPS) $(SERVER_APPS)

build/squirt: $(SQUIRT_OBJS)
	$(CC) $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt $(LIBS)

build/squirt_%: $(SQUIRT_OBJS)
	$(CC) $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt_$* $(LIBS)


build/obj/%.o: %.c squirt.h common.h Makefile
	@mkdir -p build/obj
	$(CC) -c $(CFLAGS) $*.c -o build/obj/$*.o

build/obj/amiga/%.o: %.c squirt.h common.h squirtd.h Makefile
	@mkdir -p build/obj/amiga
	vc -c $(VBCC_CFLAGS) $*.c -c -o build/obj/amiga/$*.o

build/obj/amiga.gcc/%.o: %.c squirt.h common.h squirtd.h Makefile
	@mkdir -p build/obj/amiga.gcc
	$(AMIGA_GCC) $(AMIGA_GCC_CFLAGS) $*.c -c -o build/obj/amiga.gcc/$*.o

build/amiga/squirtd.vbcc: $(SQUIRTD_AMIGA_OBJS)
	@mkdir -p build/amiga
	vc $(VBCC_CFLAGS) $(SQUIRTD_AMIGA_OBJS) -o build/amiga/squirtd.vbcc -lauto

build/amiga/squirtd: $(SQUIRTD_AMIGA_GCC_OBJS)
	@mkdir -p build/amiga
	$(AMIGA_GCC) $(AMIGA_GCC_CFLAGS) $(SQUIRTD_AMIGA_GCC_OBJS) -o build/amiga/squirtd -lamiga
	$(AMIGA_GCC_PREFIX)/bin/m68k-amigaos-strip build/amiga/squirtd

install: all
	cp $(CLIENT_APPS) /usr/local/bin/

clean:
	rm -rf build

include mingw.mk
