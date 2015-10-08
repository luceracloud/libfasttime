#
# Copyright 2015 Lucera Financial Infrastructure, LLC
#
# This software may be modified and distributed under the terms of the
# MIT license. See the LICENSE file for details.
#
PREFIX=/opt/lucera
PLATFORM_MAKEFILE=Makefile.$(shell uname -s)

include $(PLATFORM_MAKEFILE)

CC=gcc
CFLAGS=-std=c99 -g -Wall -Wextra -Werror -O2 $(PLATFORM_CFLAGS)
CPP=-DXOPEN_SOURCE=600 $(PLATFORM_CPP)
LD=$(PLATFORM_LD)
LIB_CFLAGS=$(CFLAGS) -fpic -shared
LIB_LD=$(LD) $(PLATFORM_LIB_LD)

OBJ=libfasttime.so

DBGDIR=debug
DBGOBJ32=$(DBGDIR)/$(OBJ)
DBGOBJ64=$(DBGDIR)/64/$(OBJ)
DBGOBJS=$(DBGOBJ32) $(DBGOBJ64)

RELDIR=release
RELOBJ32=$(RELDIR)/$(OBJ)
RELOBJ64=$(RELDIR)/64/$(OBJ)
RELOBJS=$(RELOBJ32) $(RELOBJ64)

TESTDIR=test
TEST32=$(TESTDIR)/fasttime_test
TEST64=$(TESTDIR)/64/fasttime_test
TESTS=$(TEST32) $(TEST64)

CP=cp
MKDIR=mkdir -p
RM=rm -rf

.PHONY: all clean debug test test-long

all:	dbg $(TESTS)

clean:
	$(RM) $(DBGDIR) $(TESTDIR)

dbg:	$(DBGOBJS)

rel:	$(RELOBJS)

install: install.$(shell uname -s)

install.com: rel
	$(MKDIR) $(LIB32_DIR) $(LIB64_DIR)
	$(CP) $(RELOBJ32) $(LIB32_DIR)
	$(CP) $(RELOBJ64) $(LIB64_DIR)

test:	all
	@echo running 32-bit test
	LD_PRELOAD=$(DBGOBJ32) $(TEST32)
	@echo running 64-bit test
	LD_PRELOAD=$(DBGOBJ64) $(TEST64)

test-long: all
	@echo running long \(5 mins\) 32-bit test
	LD_PRELOAD=$(DBGOBJ32) $(TEST32) -l 5
	@echo running long \(5 mins\) 64-bit test
	LD_PRELOAD=$(DBGOBJ64) $(TEST64) -l 5

$(DBGOBJ32): fasttime.c
	$(MKDIR) $(DBGDIR)
	$(CC) -m32 $(LIB_CFLAGS) $(CPP) $< -o $(@) $(LIB_LD)

$(DBGOBJ64): fasttime.c
	$(MKDIR) $(DBGDIR)/64
	$(CC) -m64 $(LIB_CFLAGS) $(CPP) $< -o $(@) $(LIB_LD)

$(RELOBJ32): fasttime.c
	$(MKDIR) $(RELDIR)
	$(CC) -m32 $(LIB_CFLAGS) $(CPP) -DNDEBUG $< -o $(@) $(LIB_LD)

$(RELOBJ64): fasttime.c
	$(MKDIR) $(RELDIR)/64
	$(CC) -m64 $(LIB_CFLAGS) $(CPP) -DNDEBUG $< -o $(@) $(LIB_LD)

$(TEST32): fasttime_test.c
	$(MKDIR) $(TESTDIR)
	$(CC) -m32 $(CFLAGS) $(CPP) $< -o $(@) $(DBGOBJ32) $(LD)

$(TEST64): fasttime_test.c
	$(MKDIR) $(TESTDIR)/64
	$(CC) -m64 $(CFLAGS) $(CPP) $< -o $(@) $(DBGOBJ64) $(LD)
