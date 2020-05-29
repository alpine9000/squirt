MINGW_GCC_PREFIX=/usr/local/mingw
MINGW_GCC=$(MINGW_GCC_PREFIX)/bin/x86_64-w64-mingw32-gcc

MINGW_LIBS=-lws2_32 -liconv
MINGW_GCC_CFLAGS=-O2 $(WARNINGS) -I/$(MINGW_GCC_PREFIX)/include
SQUIRT_MINGW_OBJS=$(addprefix build/obj/mingw/, $(SQUIRT_SRCS:.c=.o))
MINGW_APPS=build/mingw/squirt.exe build/mingw/squirt_exec.exe build/mingw/squirt_dir.exe build/mingw/squirt_exec.exe build/mingw/squirt_backup.exe

mingw: $(MINGW_APPS)

build/mingw/squirt: $(SQUIRT_MINGW_OBJS)
	@mkdir -p build/mingw
	$(MINGW_GCC) $(MINGW_GCC_CFLAGS) $(SQUIRT_MINGW_OBJS) -o build/mingw/squirt $(MINGW_LIBS)

build/mingw/squirt_%: $(SQUIRT_MINGW_OBJS)
	@mkdir -p build/mingw
	$(MINGW_GCC) $(MINGW_GCC_CFLAGS) $(SQUIRT_MINGW_OBJS) -o build/mingw/squirt_$* $(MINGW_LIBS)

build/obj/mingw/%.o: %.c squirt.h common.h Makefile
	@mkdir -p build/obj/mingw
	$(MINGW_GCC) -c $(MINGW_GCC_CFLAGS) $*.c -c -o build/obj/mingw/$*.o
