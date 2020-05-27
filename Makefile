SQUIRTD_SRCS=squirtd.c squirtd_exec.c
SQUIRT_SRCS=squirt.c squirt_exec.c squirt_suck.c squirt_dir.c squirt_main.c util.c

DEBUG_CFLAGS=-g -fsanitize=address -fsanitize=undefined
CFLAGS=$(DEBUG_CFLAGS) -Os  -Wall -Werror -Wall -Wpedantic -Wno-unknown-attributes -Wno-ignored-optimization-argument -Wno-unknown-pragmas  -Wmissing-field-initializers -Wfatal-errors -Wextra -Wshadow -Wuninitialized  -Wundef -Wbad-function-cast -Wparentheses -Wnull-dereference -pedantic-errors
VBCC_CFLAGS=-O1 -DAMIGA +aos68k -c99

SQUIRTD_AMIGA_OBJS=$(addprefix build/obj/amiga/, $(SQUIRTD_SRCS:.c=.o))
SQUIRTD_OBJS=$(addprefix build/obj/, $(SQUIRTD_SRCS:.c=.o))
SQUIRT_OBJS=$(addprefix build/obj/, $(SQUIRT_SRCS:.c=.o))

CLIENT_APPS=build/squirt_exec build/squirt_suck build/squirt_dir build/squirt_backup build/squirt
all: $(CLIENT_APPS) build/squirtd build/amiga/squirtd

build/squirt: $(SQUIRT_OBJS)
	gcc $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt -lncurses -liconv

build/squirt_%: $(SQUIRT_OBJS)
	gcc $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt_$* -lncurses -liconv

build/squirtd: $(SQUIRTD_OBJS)
	gcc $(CFLAGS) $(SQUIRTD_OBJS) -o build/squirtd

build/obj/%.o: %.c squirt.h common.h squirtd.h Makefile
	@mkdir -p build/obj
	gcc -c $(CFLAGS) $*.c -o build/obj/$*.o

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
