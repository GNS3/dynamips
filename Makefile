# Makefile for Dynamips 0.2.3
# Copyright (c) 2005-2006 Christophe Fillot.

# Replace x86 by amd64 for a build on x86_64.
# Use "nojit" for architectures that are not x86 or x86_64.
ARCH=x86

# Change this to 0 if your system doesn't support RFC 2553 extensions
HAS_RFC2553=1

# Current dynamips release
VERSION=0.2.3b

CC=gcc
LD=ld
RM=rm
TAR=tar
CP=cp
LEX=flex
ARCH_INC_FILE=\"$(ARCH)_trans.h\"
CFLAGS=-g -Wall -O3 -fomit-frame-pointer -DJIT_ARCH=\"$(ARCH)\" \
	-DARCH_INC_FILE=$(ARCH_INC_FILE) \
	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE \
	-DHAS_RFC2553=$(HAS_RFC2553)

ifeq ($(shell uname), FreeBSD)
   CFLAGS+=-I/usr/local/include -I/usr/local/include/libelf
   LIBS=-L/usr/local/lib -L. -lelf -pthread
else
ifeq ($(shell uname -o), Cygwin)
   CFLAGS+=-I/usr/local/include -I/usr/local/include/libelf
   LIBS=-L/usr/local/lib -L. -lelf -lpthread
else
   LIBS=-L. -lelf -lpthread
endif
endif

PROG=dynamips
PACKAGE=$(PROG)-$(VERSION)
ARCHIVE=$(PACKAGE).tar.gz

# Header and source files
HDR=mempool.h cfg_lexer.h cfg_parser.h rbtree.h hash.h utils.h \
	net.h net_io.h net_io_bridge.h atm.h \
	ptask.h dynamips.h insn_lookup.h \
	mips64.h mips64_exec.h cpu.h cp0.h memory.h device.h \
	nmc93c46.h ds1620.h pci_dev.h pcireg.h \
	dev_vtty.h dev_c7200.h dev_c7200_bay.h
SOURCES=mempool.c cfg_lexer.c cfg_parser.c rbtree.c hash.c utils.c \
	net.c net_io.c net_io_bridge.c atm.c ptask.c \
	dynamips.c insn_lookup.c mips64.c mips64_jit.c mips64_exec.c \
	cpu.c cp0.c memory.c device.c nmc93c46.c pci_dev.c pci_io.c \
	dev_zero.c dev_vtty.c dev_nvram.c dev_rom.c dev_bootflash.c \
	dev_clpd6729.c dev_iofpga.c dev_mpfpga.c dev_gt64k.c \
	dev_dec21x50.c dev_pericom.c \
	dev_c7200.c dev_c7200_bay.c dev_c7200_sram.c dev_dec21140.c \
	dev_c7200_serial.c dev_pa_a1.c \
	dev_sb1_duart.c

# Profiling
#SOURCES += profiler.c
#CFLAGS += -p -DPROFILE -DPROFILE_FILE=\"$(PROG).profile\"

ifeq ($(ARCH),x86)
HDR += x86-codegen.h x86_trans.h
SOURCES += x86_trans.c
endif

ifeq ($(ARCH),amd64)
HDR += x86-codegen.h amd64-codegen.h amd64_trans.h
SOURCES += amd64_trans.c
endif

ifeq ($(ARCH),nojit)
HDR += nojit_trans.h
SOURCES += nojit_trans.c
endif

# RAW Ethernet support for Linux
ifeq ($(shell uname), Linux)
CFLAGS += -DLINUX_ETH
HDR += linux_eth.h
SOURCES += linux_eth.c
endif

LEX_SOURCES=cfg_lexer.l

OBJS=$(SOURCES:.c=.o)
LEX_C=$(LEX_SOURCES:.l=.c)

SUPPL=Makefile ChangeLog README TODO microcode
FILE_LIST := $(HDR) $(SOURCES) $(SUPPL) \
	x86-codegen.h x86_trans.c x86_trans.h \
	amd64-codegen.h amd64_trans.c amd64_trans.h \
	nojit_trans.c nojit_trans.h linux_eth.c linux_eth.h \
	cfg_lexer.l profiler.c profiler_resolve.pl bin2c.c rom2c.c

dynamips: microcode $(LEX_C) $(OBJS)
	@echo "Linking $(PROG)"
	@$(CC) -o $(PROG) $(OBJS) $(LIBS)

.PHONY: microcode
microcode: 
	@$(CC) -Wall -o rom2c rom2c.c $(LIBS)
	@./rom2c microcode microcode_dump.inc

.PHONY: clean
clean:
	$(RM) -f microcode_dump.inc $(OBJS) $(PROG)
	$(RM) -f *~

.PHONY: package
package:
	@mkdir -p distrib/$(PACKAGE)
	@$(CP) $(FILE_LIST) distrib/$(PACKAGE)
	@cd distrib ; $(TAR) czf $(ARCHIVE) $(PACKAGE)

.SUFFIXES: .c .h .l .y .o

.c.o:
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDE) -c -o $*.o $<

.l.c:
	$(LEX) -o$*.c $<
