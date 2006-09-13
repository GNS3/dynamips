# Makefile for Dynamips 0.2.5
# Copyright (c) 2005-2006 Christophe Fillot.

# Replace x86 by amd64 for a build on x86_64.
# Use "nojit" for architectures that are not x86 or x86_64.
DYNAMIPS_ARCH?=x86

# Change this to 0 if your system doesn't support RFC 2553 extensions
HAS_RFC2553?=1

# Change this to 1 if your system has libpcap-0.9.4 or better 
# (WinPcap is used for Cygwin)
HAS_PCAP?=1

# Current dynamips release
VERSION=0.2.5
VERSION_DEV=$(VERSION)-$(shell date +%Y%m%d-%H)

# Executable binary extension
BIN_EXT?=

CC?=gcc
LD=ld
RM=rm
TAR=tar
CP=cp
LEX=flex
ARCH_INC_FILE=\"$(DYNAMIPS_ARCH)_trans.h\"
CFLAGS+=-g -Wall -O3 -fomit-frame-pointer \
	-DJIT_ARCH=\"$(DYNAMIPS_ARCH)\" \
	-DARCH_INC_FILE=$(ARCH_INC_FILE) -DDYNAMIPS_VERSION=\"$(VERSION)\" \
	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE \
	-DHAS_RFC2553=$(HAS_RFC2553)

PCAP_LIB=/usr/local/lib/libpcap.a
#PCAP_LIB=-lpcap

ifeq ($(shell uname), FreeBSD)
   PTHREAD_LIBS?=-pthread
   CFLAGS+=-I/usr/local/include -I/usr/local/include/libelf $(PTHREAD_CFLAGS)
   LIBS=-L/usr/local/lib -L. -lelf $(PTHREAD_LIBS)
else
ifeq ($(shell uname -s), Darwin)
   CFLAGS+=-I/usr/local/include -mdynamic-no-pic
   LIBS=-L/usr/local/lib -L. -lelf -lpthread
else
ifeq ($(shell uname -o), Cygwin)
   CFLAGS+=-I/usr/local/include -I/usr/local/include/libelf -DCYGWIN
   LIBS=-L/usr/local/lib -L. -lelf -lpthread
   PCAP_LIB=-lpacket -lwpcap
else
   CFLAGS+=-I/usr/include/libelf
   LIBS=-L. /usr/lib/libelf.a -lpthread
endif
endif
endif

PROG=dynamips$(BIN_EXT)
PACKAGE=$(PROG)-$(VERSION)
ARCHIVE=$(PACKAGE).tar.gz

PACKAGE_DEV=$(PROG)-$(VERSION_DEV)
ARCHIVE_DEV=$(PACKAGE_DEV).tar.gz

# Header and source files
HDR=mempool.h registry.h rbtree.h hash.h utils.h parser.h \
	crc.h base64.h net.h net_io.h net_io_bridge.h net_io_filter.h \
	atm.h frame_relay.h eth_switch.h \
	ptask.h hypervisor.h dynamips.h insn_lookup.h \
	vm.h mips64.h mips64_exec.h cpu.h cp0.h memory.h device.h \
	nmc93c46.h cisco_eeprom.h ds1620.h pci_dev.h pci_io.h \
	dev_dec21140.h dev_am79c971.h dev_mueslix.h \
	dev_vtty.h dev_c7200.h dev_c3600.h dev_c3600_bay.h
SOURCES=mempool.c registry.c rbtree.c hash.c utils.c parser.c ptask.c \
	crc.c base64.c net.c net_io.c net_io_bridge.c net_io_filter.c \
	atm.c frame_relay.c eth_switch.c \
	dynamips.c insn_lookup.c vm.c mips64.c mips64_jit.c mips64_exec.c \
	cpu.c cp0.c memory.c device.c nmc93c46.c cisco_eeprom.c \
	pci_dev.c pci_io.c \
	dev_zero.c dev_vtty.c dev_ram.c dev_rom.c dev_nvram.c dev_bootflash.c \
	dev_remote.c dev_clpd6729.c dev_pcmcia_disk.c dev_gt64k.c \
	dev_plx9060.c dev_dec21x50.c dev_pericom.c dev_ap1011.c \
	dev_ns16552.c dev_dec21140.c dev_am79c971.c dev_mueslix.c \
	dev_c3600.c dev_c3600_bay.c dev_c3600_iofpga.c \
	dev_c3600_eth.c dev_c3600_serial.c \
	dev_c7200.c dev_c7200_iofpga.c dev_c7200_mpfpga.c \
	dev_c7200_sram.c dev_c7200_eth.c dev_c7200_serial.c dev_c7200_pos.c \
	dev_c7200_bri.c \
	dev_pa_a1.c dev_sb1.c dev_sb1_io.c dev_sb1_pci.c hypervisor.c \
	hv_nio.c hv_nio_bridge.c hv_frsw.c hv_atmsw.c hv_ethsw.c \
	hv_vm.c hv_c7200.c hv_c3600.c

# Profiling
#SOURCES += profiler.c
#CFLAGS += -p -DPROFILE -DPROFILE_FILE=\"$(PROG).profile\"

ifeq ($(DYNAMIPS_ARCH),x86)
HDR += x86-codegen.h x86_trans.h
SOURCES += x86_trans.c
ASMSRC += x86_asm.S
CFLAGS += -DFAST_ASM
endif

ifeq ($(DYNAMIPS_ARCH),amd64)
HDR += x86-codegen.h amd64-codegen.h amd64_trans.h
SOURCES += amd64_trans.c
endif

ifeq ($(DYNAMIPS_ARCH),nojit)
HDR += nojit_trans.h
SOURCES += nojit_trans.c
endif

# RAW Ethernet support for Linux
ifeq ($(shell uname), Linux)
CFLAGS += -DLINUX_ETH
HDR += linux_eth.h
SOURCES += linux_eth.c
endif

# Generic Ethernet support with libpcap (0.9+)
ifeq ($(HAS_PCAP), 1)
CFLAGS += -DGEN_ETH
HDR += gen_eth.h
SOURCES += gen_eth.c

LIBS += $(PCAP_LIB)
endif

C_OBJS=$(SOURCES:.c=.o)
A_OBJS=$(ASMSRC:.S=.o)
LEX_C=$(LEX_SOURCES:.l=.c)

SUPPL=Makefile ChangeLog COPYING README README.hypervisor TODO \
	dynamips.1 nvram_export.1 hypervisor_mode.7 microcode
FILE_LIST := $(HDR) $(SOURCES) $(SUPPL) \
	x86-codegen.h x86_trans.c x86_trans.h x86_asm.S \
	amd64-codegen.h amd64_trans.c amd64_trans.h \
	nojit_trans.c nojit_trans.h asmdefs.c \
	linux_eth.c linux_eth.h gen_eth.c gen_eth.h \
	profiler.c profiler_resolve.pl bin2c.c rom2c.c \
	nvram_export.c

.PHONY: all
all: $(PROG) nvram_export

$(PROG): microcode_dump.inc asmdefs.h $(LEX_C) $(C_OBJS) $(A_OBJS)
	@echo "Linking $@"
	@$(CC) -o $@ $(C_OBJS) $(A_OBJS) $(LIBS)

rom2c$(BIN_EXT): rom2c.c
	@echo "Linking $@"
	@$(CC) -Wall $(CFLAGS) -o $@ rom2c.c $(LIBS)

microcode_dump.inc: rom2c$(BIN_EXT) microcode
	@$(CC) -Wall $(CFLAGS) -o $@ rom2c.c $(LIBS)
	@./rom2c microcode microcode_dump.inc

asmdefs$(BIN_EXT): asmdefs.c mips64.h
	@echo "Linking $@"
	@$(CC) -Wall $(CFLAGS) -o $@ asmdefs.c

asmdefs.h: asmdefs$(BIN_EXT)
	@echo "Building assembly definitions header file"
	@./asmdefs

nvram_export$(BIN_EXT): nvram_export.c
	@echo "Linking $@"
	@$(CC) -Wall $(CFLAGS) -o $@ nvram_export.c

.PHONY: clean
clean:
	$(RM) -f rom2c$(BIN_EXT) microcode_dump.inc asmdefs$(BIN_EXT) \
	asmdefs.h $(C_OBJS) $(A_OBJS) $(PROG)
	$(RM) -f *~

.PHONY: package
package:
	@mkdir -p distrib/$(PACKAGE)
	@$(CP) $(FILE_LIST) distrib/$(PACKAGE)
	@cd distrib ; $(TAR) czf $(ARCHIVE) $(PACKAGE)

.PHONY: packdev
packdev:
	@mkdir -p distrib/$(PACKAGE_DEV)
	@$(CP) $(FILE_LIST) distrib/$(PACKAGE_DEV)
	@cd distrib ; $(TAR) czf $(ARCHIVE_DEV) $(PACKAGE_DEV)

.SUFFIXES: .c .h .S .l .y .o

.S.o:
	@echo "Assembling $<"
	@$(CC) $(CFLAGS) $(INCLUDE) -c -o $*.o $<

.c.o:
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDE) -c -o $*.o $<

.l.c:
	$(LEX) -o$*.c $<
