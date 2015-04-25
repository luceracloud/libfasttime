PLATFORM_MAKEFILE=Makefile.$(shell uname -s)

include $(PLATFORM_MAKEFILE)

CC=gcc
CFLAGS=-std=c99 -g -Wall -Werror -O2 $(PLATFORM_CFLAGS)
CPP=-DXOPEN_SOURCE=600 $(PLATFORM_CPP)
DBGDIR=debug
DBGOBJ32=$(DBGDIR)/$(OBJ)
DBGOBJ64=$(DBGDIR)/64/$(OBJ)
DBGOBJS=$(DBGOBJ32) $(DBGOBJ64)
LD=$(PLATFORM_LD)
LIB_CFLAGS=$(CFLAGS) -fpic -shared
LIB_LD=$(LD) $(PLATFORM_LIB_LD)
MKDIR=mkdir -p
OBJ=libfasttime.so
RM=rm -rf
TESTDIR=test
TEST32=$(TESTDIR)/fasttime_test
TEST64=$(TESTDIR)/64/fasttime_test
TESTS=$(TEST32) $(TEST64)

.PHONY: all clean debug test

all:	dbg $(TESTS)

clean:
	$(RM) $(DBGDIR) $(TESTDIR)

dbg:	$(DBGOBJS)

test:	all
	@echo running 32-bit test
	LD_PRELOAD=$(DBGOBJ32) $(TEST32)
	@echo running 64-bit test
	LD_PRELOAD=$(DBGOBJ64) $(TEST64)

$(DBGOBJ32): fasttime.c
	$(MKDIR) $(DBGDIR)
	$(CC) -m32 $(LIB_CFLAGS) $(CPP) $< -o $(@) $(LIB_LD)

$(DBGOBJ64): fasttime.c
	$(MKDIR) $(DBGDIR)/64
	$(CC) -m64 $(LIB_CFLAGS) $(CPP) $< -o $(@) $(LIB_LD)

$(TEST32): fasttime_test.c
	$(MKDIR) $(TESTDIR)
	$(CC) -m32 $(CFLAGS) $(CPP) $< -o $(@) $(LD)

$(TEST64): fasttime_test.c
	$(MKDIR) $(TESTDIR)/64
	$(CC) -m64 $(CFLAGS) $(CPP) $< -o $(@) $(LD)
