RELEASE=true
CLIENT_APPS=squirt_exec squirt_suck squirt_dir squirt_backup squirt squirt_cli squirt_cwd squirt_restore

include platforms.mk

SQUIRT_SRCS=squirt.c exec.c suck.c dir.c main.c cli.c cwd.c srl.c util.c argv.c backup.c restore.c exall.c protect.c crc32.c
SUM_SRCS=sum.c crc32.c
HEADERS=main.h squirt.h exec.h cwd.h dir.h srl.h cli.h backup.h argv.h common.h util.h main.h suck.h restore.h exall.h protect.h win_compat.h
COMMON_DEPS=Makefile platforms.mk mingw.mk

DEBUG_CFLAGS=-g $(STATIC_ANALYZE)
WARNINGS=-Wno-error=format -Wno-format -Wall -Werror -Wall -Wpedantic -Wno-unknown-attributes -Wno-ignored-optimization-argument -Wno-unknown-pragmas  -Wmissing-field-initializers -Wfatal-errors -Wextra -Wshadow -Wuninitialized  -Wundef -Wbad-function-cast -Wparentheses -Wnull-dereference -pedantic-errors  -Wno-strict-prototypes

ifeq ($(RELEASE),true)
CFLAGS=$(WARNINGS) -O2
LDFLAGS=-s
else
CFLAGS=$(DEBUG_CFLAGS) $(WARNINGS)
LDFLAGS=
endif

SQUIRT_OBJS=$(addprefix build/obj/, $(SQUIRT_SRCS:.c=.o))
SUM_OBJS=$(addprefix build/obj/, $(SUM_SRCS:.c=.o))
HOST_CLIENT_APPS=$(addprefix build/, $(CLIENT_APPS))
AMIGA_APPS=build/amiga/squirtd build/amiga/ssum build/amiga/skill build/amiga/sps

RELEASE_VERSION=v0.4
RELEASE_DIR=release/squirt

all: $(HOST_CLIENT_APPS) $(AMIGA_APPS)

release: all mingw musl
	rm -rf release
	mkdir release
	mkdir $(RELEASE_DIR)
	mkdir $(RELEASE_DIR)/osx-sequoia
	mkdir $(RELEASE_DIR)/amiga
	mkdir $(RELEASE_DIR)/w32-x86-64
	mkdir $(RELEASE_DIR)/linux
	cp LICENSE README.md $(RELEASE_DIR)/osx-sequoia
	cp LICENSE README.md $(RELEASE_DIR)/w32-x86-64
	cp LICENSE README.md $(RELEASE_DIR)/amiga
	cp LICENSE README.md $(RELEASE_DIR)/linux
	cp doc/inetd.md $(RELEASE_DIR)/amiga
	cp $(HOST_CLIENT_APPS) $(RELEASE_DIR)/osx-sequoia
	cp $(MINGW_APPS) $(RELEASE_DIR)/w32-x86-64
	cp $(AMIGA_APPS) $(RELEASE_DIR)/amiga
	cp $(MUSL_APPS) $(RELEASE_DIR)/linux
	cd $(RELEASE_DIR) && tar zcfv ../squirt-osx-$(RELEASE_VERSION).tgz osx-sequoia
	cd $(RELEASE_DIR) && tar zcfv ../squirt-linux-$(RELEASE_VERSION).tgz linux
	cd $(RELEASE_DIR) && lha a ../squirt-amiga-$(RELEASE_VERSION).lha amiga
	cd $(RELEASE_DIR) && zip -r ../squirt-w32-x86-64-$(RELEASE_VERSION).zip w32-x86-64

client: $(HOST_CLIENT_APPS)

build/sum: $(SUM_OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(SUM_OBJS) -o build/sum $(LIBS)

build/squirt: $(SQUIRT_OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt $(LIBS)

build/squirt%: $(SQUIRT_OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) $(SQUIRT_OBJS) -o build/squirt$* $(LIBS)

build/obj/%.o: %.c $(HEADERS) $(COMMON_DEPS)
	@mkdir -p build/obj
	$(CC) -c $(CFLAGS) $*.c -o build/obj/$*.o

build/obj/amiga/%.o: %.c $(COMMON_DEPS)
	@mkdir -p build/obj/amiga
	$(AMIGA_BIN) $(AMIGA_GCC_CFLAGS) $*.c -c -o build/obj/amiga/$*.o

build/amiga/squirtd: squirtd.c $(COMMON_DEPS)
	@mkdir -p build/amiga
	$(AMIGA_BIN) squirtd.c -s $(AMIGA_SQUIRTD_CFLAGS) $(SQUIRTD_AMIGA_GCC_OBJS) -o build/amiga/squirtd -lamiga

build/amiga/ssum: build/obj/amiga/crc32.o build/obj/amiga/sum.o crc32.h $(COMMON_DEPS)
	@mkdir -p build/amiga
	$(AMIGA_BIN) $(AMIGA_GCC_CFLAGS) -s build/obj/amiga/crc32.o build/obj/amiga/sum.o -o build/amiga/ssum -lamiga

build/amiga/skill: kill.c $(COMMON_DEPS)
	@mkdir -p build/amiga
	$(AMIGA_BIN) $(AMIGA_SQUIRTD_CFLAGS) -s kill.c -o build/amiga/skill -lamiga

build/amiga/sps: ps.c $(COMMON_DEPS)
	@mkdir -p build/amiga
	$(AMIGA_BIN) $(AMIGA_SQUIRTD_CFLAGS) -s ps.c -o build/amiga/sps -lamiga

install: all
	cp $(HOST_CLIENT_APPS) /usr/local/bin/

clean:
	rm -rf build

include mingw.mk
include musl.mk
