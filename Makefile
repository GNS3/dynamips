# Makefile for Dynamips

# Host CPU selection
#   - Use "x86" for a build on x86 (32-bits)
#   - Use "amd64" for a build on x86_64 (64-bits)
#   - Use "nojit" for unsupported architectures.
ifeq ($(shell arch),x86_64)
export DYNAMIPS_ARCH?=amd64
else
ifeq ($(shell arch),i686)
export DYNAMIPS_ARCH?=x86
else
export DYNAMIPS_ARCH?=nojit
endif
endif

# For MAC x64 you can compile the "unstable" version, which should work
# fine, or use stable if you prefer.
# For all other targets you should really choose "stable" here.
# Unstable has some optimizations in it, plus new TLA code.
# However it does not seem stable for non-mac users
export DYNAMIPS_CODE?=stable

# Change this to 0 if your system doesn't support RFC2553 extensions
export HAS_RFC2553?=1

# Change this to 1 if your system has libpcap-0.9.4 or better 
# (WinPcap is used for Cygwin)
export HAS_PCAP?=1

# Change this to 1 if your system has posix_memalign
export HAS_POSIX_MEMALIGN?=1

# Current dynamips release
export VERSION_TRAIN=0.2.12
export VERSION_SUB=

# Executable binary extension and prefix directory
export PREFIX?=/usr
export BIN_EXT?=


.PHONY: all dynamips.stable dynamips.unstable both install clean
all: dynamips.$(DYNAMIPS_CODE)

dynamips.stable:
	$(MAKE) -C stable
	mv stable/dynamips$(BIN_EXT) dynamips.stable$(BIN_EXT)
	mv stable/nvram_export$(BIN_EXT) nvram_export.stable$(BIN_EXT) 

dynamips.unstable:
	$(MAKE) -C unstable
	mv unstable/dynamips$(BIN_EXT) dynamips.unstable$(BIN_EXT)
	mv unstable/nvram_export$(BIN_EXT) nvram_export.unstable$(BIN_EXT) 

# target to facilitate test compilations
both: dynamips.stable dynamips.unstable

install: dynamips.$(DYNAMIPS_CODE)$(BIN_EXT) nvram_export.$(DYNAMIPS_CODE)$(BIN_EXT)
	@echo "Installing"
	
	cp dynamips.$(DYNAMIPS_CODE)$(BIN_EXT) dynamips$(BIN_EXT)
	cp nvram_export.$(DYNAMIPS_CODE)$(BIN_EXT) nvram_export$(BIN_EXT)
	
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1 $(DESTDIR)$(PREFIX)/share/man/man7
	install dynamips$(BIN_EXT) nvram_export$(BIN_EXT) $(DESTDIR)$(PREFIX)/bin/
	install -m644 man/dynamips.1        $(DESTDIR)$(PREFIX)/share/man/man1
	install -m644 man/nvram_export.1    $(DESTDIR)$(PREFIX)/share/man/man1
	install -m644 man/hypervisor_mode.7 $(DESTDIR)$(PREFIX)/share/man/man7

clean:
	$(MAKE) -C stable clean
	$(MAKE) -C unstable clean
	$(RM) -f dynamips$(BIN_EXT) dynamips.stable$(BIN_EXT) dynamips.unstable$(BIN_EXT)
	$(RM) -f nvram_export$(BIN_EXT) nvram_export.stable$(BIN_EXT) nvram_export.unstable$(BIN_EXT)

