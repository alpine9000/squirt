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

MINGW_LIBS=-lws2_32 -liconv -static

ifeq ($(PLATFORM),osx)
# OSX
CC=gcc -I/opt/homebrew/opt/libiconv/include
LDFLAGS=
STATIC_ANALYZE=-fsanitize=address -fsanitize=undefined
ifeq ($(RELEASE),true)
#LIBS=-L/opt/homebrew/opt/libiconv/lib -liconv -lcharset -ltermcap
LIBS=/opt/homebrew/Cellar/libiconv/1.17/lib/libiconv.a 
else
LIBS=-liconv -ltermcap
endif
MINGW_GCC_PREFIX=/usr/local/mingw
MINGW_GCC=$(MINGW_GCC_PREFIX)/bin/x86_64-w64-mingw32-gcc
MINGW_LIB_PATH=-L$(MINGW_GCC_PREFIX)/lib
MUSL_GCC_PREFIX=/opt/homebrew
MUSL_GCC=$(MUSL_GCC_PREFIX)/bin/x86_64-unknown-linux-musl-gcc
MUSL_LIB_PATH=-L$(MINGW_GCC_PREFIX)/lib
else ifeq ($(PLATFORM),raspberry_pi)
# Raspberry Pi
CC=gcc
LIBS=
else ifeq ($(PLATFORM),linux)
# Linux
ifeq ($(RELEASE),true)
else
CC=gcc-10
endif
STATIC_ANALYZE=-fanalyzer -fsanitize=address -fsanitize=undefined
LIBS=
MINGW_GCC_PREFIX=/usr/x86_64-w64-mingw32
MINGW_GCC=x86_64-w64-mingw32-gcc
else ifeq ($(PLATFORM),mingw64)
# Mingw64
CC=gcc
CLIENT_APPS:=$(addsuffix .exe, $(CLIENT_APPS))
LIBS=$(MINGW_LIBS)
STATIC_ANALYZE=
endif

# Use environment variable for Amiga GCC path if set, otherwise use default
ifndef AMIGA_GCC
AMIGA_GCC_PREFIX=/usr/local/amiga/bebbo
else
AMIGA_GCC_PREFIX=$(AMIGA_GCC)
endif
AMIGA_BIN=$(AMIGA_GCC_PREFIX)/bin/m68k-amigaos-gcc -I$(AMIGA_GCC_PREFIX)/m68k-amigaos/ndk-include/

AMIGA_GCC_CFLAGS=-DAMIGA -Os -msmall-code -fomit-frame-pointer -noixemul $(WARNINGS)
AMIGA_SQUIRTD_CFLAGS=$(AMIGA_GCC_CFLAGS) -fwhole-program
