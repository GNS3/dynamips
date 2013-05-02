# Makefile for Dynamips 0.2.8

# Host CPU selection
#   - Use "x86" for a build on x86 (32-bits)
#   - Use "amd64" for a build on x86_64 (64-bits)
#   - Use "ppc32" for a build on powerpc (32-bits)
#   - Use "nojit" for unsupported architectures.
export DYNAMIPS_ARCH?=x86

# Do you want to use lib (for 32 bit compiling) or lib64
export DYNAMIPS_LIB?=lib
#export DYNAMIPS_LIB?=lib64

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
export VERSION_TRAIN=0.2.8
export VERSION_SUB=-RC6

# Executable binary extension
export DESTDIR?=/usr
export BIN_EXT?=


.PHONY: all dynamips.stable dynamips.unstable
all: dynamips.$(DYNAMIPS_CODE)

dynamips.stable:
	$(MAKE) -C stable
	mv stable/dynamips dynamips.$(DYNAMIPS_CODE)

dynamips.unstable:
	$(MAKE) -C unstable
	mv unstable/dynamips dynamips.$(DYNAMIPS_CODE)

install: dynamips.$(DYNAMIPS_CODE)
	@echo "Installing"
	install -d $(DESTDIR)/bin $(DESTDIR)/man/man1 $(DESTDIR)/man/man7 $(DESTDIR)/etc
	cp dynamips.$(DYNAMIPS_CODE) dynamips
	install dynamips $(DYNAMIPS_CODE)/nvram_export   $(DESTDIR)/bin/
	rm -f dynamips
	install -m644 dynamips.1        $(DESTDIR)/man/man1
	install -m644 nvram_export.1    $(DESTDIR)/man/man1
	install -m644 hypervisor_mode.7 $(DESTDIR)/man/man7


.PHONY: clean
clean:
	$(MAKE) -C stable clean
	$(MAKE) -C unstable clean
	$(RM) -f dynamips.stable dynamips.unstable

