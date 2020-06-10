
MINGW_GCC_CFLAGS=-O2 $(WARNINGS) -I/$(MINGW_GCC_PREFIX)/include
MINGW_APPS=$(addsuffix .exe, $(addprefix build/mingw/, $(CLIENT_APPS)))
SQUIRT_MINGW_OBJS=$(addprefix build/obj/mingw/, $(SQUIRT_SRCS:.c=.o))

mingw: $(MINGW_APPS) build/mingw/libreadline8.dll build/mingw/libiconv-2.dll

build/mingw/%.dll: support/%.dll
	@mkdir -p build/mingw
	cp support/$*.dll build/mingw/$*.dll

build/mingw/squirt.exe: $(SQUIRT_MINGW_OBJS)
	@mkdir -p build/mingw
	$(MINGW_GCC) $(MINGW_GCC_CFLAGS) $(SQUIRT_MINGW_OBJS) -o build/mingw/squirt $(MINGW_LIBS)

build/mingw/squirt_%: $(SQUIRT_MINGW_OBJS)
	@mkdir -p build/mingw
	$(MINGW_GCC) $(MINGW_GCC_CFLAGS) $(SQUIRT_MINGW_OBJS) -o build/mingw/squirt_$* $(MINGW_LIBS)

build/obj/mingw/%.o: %.c $(HEADERS) common.h Makefile
	@mkdir -p build/obj/mingw
	$(MINGW_GCC) -c $(MINGW_GCC_CFLAGS) $*.c -c -o build/obj/mingw/$*.o
