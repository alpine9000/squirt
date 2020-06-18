UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
PLATFORM=osx
endif

ifeq ($(UNAME_S),Linux)
ifneq (,$(findstring arm,$(UNAME_M)))
PLATFORM=raspberry_pi
else
PLATFORM=linux
endif
endif

ifneq (,$(findstring MINGW64,$(UNAME_S)))
PLATFORM=mingw64
endif

MINGW_LIBS=-lws2_32 -liconv -lreadline

ifeq ($(PLATFORM),osx)
# OSX
CC=gcc
STATIC_ANALYZE=-fsanitize=address -fsanitize=undefined
ifeq ($(RELEASE),true)
LIBS=-liconv -ltermcap -lreadline-static
else
LIBS=-liconv -ltermcap -lreadline
endif
MINGW_GCC_PREFIX=/usr/local/mingw
MINGW_GCC=$(MINGW_GCC_PREFIX)/bin/x86_64-w64-mingw32-gcc
else ifeq ($(PLATFORM),raspberry_pi)
# Raspberry Pi
CC=gcc
LIBS=-lreadline
else ifeq ($(PLATFORM),linux)
# Linux
CC=gcc-10
STATIC_ANALYZE=-fanalyzer -fsanitize=address -fsanitize=undefined
LIBS=-lreadline
MINGW_GCC_PREFIX=/usr/x86_64-w64-mingw32
MINGW_GCC=x86_64-w64-mingw32-gcc
else ifeq ($(PLATFORM),mingw64)
# Mingw64
CC=gcc
CLIENT_APPS:=$(addsuffix .exe, $(CLIENT_APPS))
LIBS=$(MINGW_LIBS)
STATIC_ANALYZE=
endif

AMIGA_GCC_PREFIX=/usr/local/amiga/bebbo
AMIGA_GCC=$(AMIGA_GCC_PREFIX)/bin/m68k-amigaos-gcc -I$(AMIGA_GCC_PREFIX)/m68k-amigaos/ndk-include/

AMIGA_GCC_CFLAGS=-DAMIGA -Os -msmall-code -fomit-frame-pointer -noixemul $(WARNINGS)
AMIGA_SQUIRTD_CFLAGS=$(AMIGA_GCC_CFLAGS) -fwhole-program
