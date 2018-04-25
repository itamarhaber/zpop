# Setting the DEBUG env variable to 1 with -O0
ifndef DEBUG
	DEBUG = 0
endif
DEBUGFLAGS = -g -ggdb -O2
ifeq ($(DEBUG), 1)
	DEBUGFLAGS = -g -ggdb -O0
endif

# Find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
CFLAGS = -Wall $(DEBUGFLAGS) -fPIC -std=gnu99  -D_GNU_SOURCE
CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?=  -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -shared -Bsymbolic -Bsymbolic-functions
else
	CFLAGS += -mmacosx-version-min=10.6
	SHOBJ_CFLAGS ?= -dynamic -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -dylib -exported_symbol _RedisModule_OnLoad -macosx_version_min 10.6
endif
export CFLAGS
export DEBUGFLAGS

SOURCEDIR=$(shell pwd -P)
CC_SOURCES = $(wildcard $(SOURCEDIR)/*.c) 
CC_OBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(SOURCEDIR)/%.o, $(CC_SOURCES))
MODULE=zpop.so

all: $(MODULE)

$(MODULE): zpop.c $(CC_OBJECTS)
	$(LD) -o $@ $(CC_OBJECTS) $(LIBS) $(SHOBJ_LDFLAGS) -lc -lm

clean:
	rm -rvf *.xo *.so *.o *.a
