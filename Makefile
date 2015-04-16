PLATFORM_MAKEFILE=$(uname -s)

include $(PLATFORM_MAKEFILE)

CC=gcc
CFLAGS=-std=c99 -fpic -shared -Wall -Werror -O2
LD=

libfasttime.so: fasttime.c
	$(CC) -m64 $(CFLAGS) $(PLATFORM_CFLAGS) fasttime.c -o $(@) $(LD) $(PLATFORM_LD)
