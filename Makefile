SQUIRTD_SRCS=squirtd.c squirtd_exec.c
SQUIRT_SRCS=squirt.c squirt_exec.c squirt_suck.c squirt_dir.c squirt_main.c util.c

CC=gcc 
DEBUG_CFLAGS=-g -fsanitize=address -fsanitize=undefined #-fanalyzer
WARNINGS=-Wno-error=format -Wno-format -Wall -Werror -Wall -Wpedantic -Wno-unknown-attributes -Wno-ignored-optimization-argument -Wno-unknown-pragmas  -Wmissing-field-initializers -Wfatal-errors -Wextra -Wshadow -Wuninitialized  -Wundef -Wbad-function-cast -Wparentheses -Wnull-dereference -pedantic-errors 
LIBS=-lncurses -liconv
CFLAGS=$(DEBUG_CFLAGS) -Os $(WARNINGS)
VBCC_CFLAGS=-O1 -DAMIGA +aos68k -c99

SQUIRTD_AMIGA_OBJS=$(addprefix build/obj/amiga/, $(SQUIRTD_SRCS:.c=.o))
SQUIRTD_OBJS=$(addprefix build/obj/, $(SQUIRTD_SRCS:.c=.o))
SQUIRT_OBJS=$(addprefix build/obj/, $(SQUIRT_SRCS:.c=.o))

CLIENT_APPS=build/squirt_exec build/squirt_suck build/squirt_dir build/squirt_backup build/squirt
SERVER_APPS=build/squirtd build/amiga/squirtd

all: $(CLIENT_APPS) $(SERVER_APPS)

build/squirt: $(SQUIRT_OBJS)
	$(CC) $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt $(LIBS)

build/squirt_%: $(SQUIRT_OBJS)
	$(CC) $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt_$* $(LIBS)

build/squirtd: $(SQUIRTD_OBJS)
	$(CC) $(CFLAGS) $(SQUIRTD_OBJS) -o build/squirtd

build/obj/%.o: %.c squirt.h common.h squirtd.h Makefile
	@mkdir -p build/obj
	$(CC) -c $(CFLAGS) $*.c -o build/obj/$*.o

build/obj/amiga/%.o: %.c squirt.h common.h squirtd.h Makefile
	@mkdir -p build/obj/amiga
	vc -c $(VBCC_CFLAGS) $*.c -c -o build/obj/amiga/$*.o

build/amiga/squirtd: $(SQUIRTD_AMIGA_OBJS)
	@mkdir -p build/amiga
	vc $(VBCC_CFLAGS) $(SQUIRTD_AMIGA_OBJS) -o build/amiga/squirtd -lauto

install: all
	cp $(CLIENT_APPS) /usr/local/bin/

clean:
	rm -rf build
