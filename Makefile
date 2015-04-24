PLATFORM_MAKEFILE=Makefile.$(shell uname -s)

include $(PLATFORM_MAKEFILE)

CC=gcc
CFLAGS=-std=c99 -g -Wall -Werror -O2 $(PLATFORM_CFLAGS)
CPP=-DXOPEN_SOURCE=600 $(PLATFORM_CPP)
LD=$(PLATFORM_LD)

LIB_CFLAGS=$(CFLAGS) -fpic -shared
LIB_LD=$(LD) $(PLATFORM_LIB_LD)

libfasttime.so: fasttime.c
	$(CC) -m64 $(LIB_CFLAGS) $(CPP) $< -o $(@) $(LIB_LD)

fasttime_test: fasttime_test.c
	$(CC) -m64 $(CFLAGS) $(CPP) $< -o $(@) $(LD)
