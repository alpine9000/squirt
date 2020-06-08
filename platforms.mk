UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

ifeq ($(UNAME_S),Darwin)
PLATFORM=osx
endif
ifeq ($(UNAME_S),Linux)
ifeq (,$(findstring "arm",$(UNAME_M)))
PLATFORM=raspberry_pi
else
PLATFORM=linux
endif
endif
