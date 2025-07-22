
MUSL_GCC_CFLAGS=-O2 -s $(WARNINGS) -I/$(MUSL_GCC_PREFIX)/include
MUSL_APPS=$(addprefix build/musl/, $(CLIENT_APPS))
SQUIRT_MUSL_OBJS=$(addprefix build/obj/musl/, $(SQUIRT_SRCS:.c=.o))

musl: $(MUSL_APPS)

build/musl/squirt: $(SQUIRT_MUSL_OBJS)
	@mkdir -p build/musl
	$(MUSL_GCC) -static $(MUSL_GCC_CFLAGS) $(SQUIRT_MUSL_OBJS) -o build/musl/squirt $(MUSL_LIB_PATH) $(MUSL_LIBS)

build/musl/squirt_%: $(SQUIRT_MUSL_OBJS)
	@mkdir -p build/musl
	$(MUSL_GCC) -static $(MUSL_GCC_CFLAGS) $(SQUIRT_MUSL_OBJS) -o build/musl/squirt_$* $(MUSL_LIB_PATH) $(MUSL_LIBS)

build/obj/musl/%.o: %.c $(HEADERS) common.h Makefile
	@mkdir -p build/obj/musl
	$(MUSL_GCC) -c $(MUSL_GCC_CFLAGS) $*.c -c -o build/obj/musl/$*.o
