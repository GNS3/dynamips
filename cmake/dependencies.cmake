# dynamips - check dependencies
#  - libdl           : maybe required
#  - librt           : maybe required
#  - libsocket       : maybe required
#  - libelf          : required
#  - libuuid         : required
#  - pthreads        : required
#  - libpcap/winpcap : optional
# accumulators:
#  - DYNAMIPS_FLAGS
#  - DYNAMIPS_DEFINITIONS
#  - DYNAMIPS_INCLUDES
#  - DYNAMIPS_LIBRARIES
# XXX assumes utils.cmake was included

message ( STATUS "dependencies - BEGIN" )

# gnu compiler
if ( NOT CMAKE_COMPILER_IS_GNUCC AND NOT ANY_COMPILER )
   print_variables (
      CMAKE_COMPILER_IS_GNUCC
      CMAKE_C_COMPILER
      CMAKE_C_COMPILER_ABI
      CMAKE_C_COMPILER_ID
      CMAKE_C_COMPILER_VERSION
      )
   message ( FATAL_ERROR
      "Not a GNU C compiler. "
      "The source and build system assumes gcc so it might not compile. "
      "Invoke cmake with -DANY_COMPILER=1 to skip this check. "
      )
endif ( NOT CMAKE_COMPILER_IS_GNUCC AND NOT ANY_COMPILER )

set ( DYNAMIPS_FLAGS -Wall -O2 -fomit-frame-pointer )
set ( DYNAMIPS_DEFINITIONS )
set ( DYNAMIPS_INCLUDES )
set ( DYNAMIPS_LIBRARIES ${CMAKE_DL_LIBS} )
if ( CYGWIN )
   list ( APPEND DYNAMIPS_FLAGS -static -static-libgcc )
endif ( CYGWIN )
macro ( set_cmake_required )
   set ( CMAKE_REQUIRED_FLAGS       ${DYNAMIPS_FLAGS} )
   set ( CMAKE_REQUIRED_DEFINITIONS ${DYNAMIPS_DEFINITIONS} )
   set ( CMAKE_REQUIRED_INCLUDES    ${DYNAMIPS_INCLUDES} )
   set ( CMAKE_REQUIRED_LIBRARIES   ${DYNAMIPS_LIBRARIES} )
endmacro ( set_cmake_required )

# Target architecture:
#  - Use "amd64" to build for x86_64 (64-bit)
#  - Use "x86" to build for x86 (32-bit)
#  - Use "nojit" to build for other architectures (no recompilation)
# Based on https://github.com/petroules/solar-cmake/blob/master/TargetArch.cmake
set ( _code "
#if defined(__x86_64) || defined(__x86_64__) || defined(__amd64) || defined(_M_X64)
int main (void) { return 0; }
#else
#error cmake_FAIL
#endif
" )
set_cmake_required ()
list ( INSERT CMAKE_REQUIRED_FLAGS 0 -m64 )
check_c_source_compiles ( "${_code}" ARCH_AMD64 )
set ( _code "
#if defined(__i386) || defined(__i386__) || defined(_M_IX86)
int main (void) { return 0; }
#else
#error cmake_FAIL
#endif
" )
set_cmake_required ()
list ( INSERT CMAKE_REQUIRED_FLAGS 0 -m32 )
check_c_source_compiles ( "${_code}" ARCH_X86 )
if ( ARCH_AMD64 )
   set ( _default "amd64" )
elseif ( ARCH_X86 )
   set ( _default "x86" )
else ()
   set ( _default "nojit" )
endif ()
set ( DYNAMIPS_ARCH "${_default}" CACHE STRING "Target architecture (amd64;x86;nojit)" )
set_property ( CACHE DYNAMIPS_ARCH PROPERTY STRINGS "amd64" "x86" "nojit" )
if ( NOT DYNAMIPS_ARCH )
   set ( DYNAMIPS_ARCH "${_default}" )
endif ()
if ( "amd64" STREQUAL "${DYNAMIPS_ARCH}" AND ARCH_AMD64 )
   list ( INSERT DYNAMIPS_FLAGS 0 -m64 )
elseif ( "x86" STREQUAL "${DYNAMIPS_ARCH}" AND ARCH_X86 )
   list ( INSERT DYNAMIPS_FLAGS 0 -m32 )
elseif ( NOT "nojit" STREQUAL "${DYNAMIPS_ARCH}" )
   print_variables ( ARCH_AMD64 ARCH_X86 DYNAMIPS_ARCH )
   message ( FATAL_ERROR "cannot build target arch DYNAMIPS_ARCH=${DYNAMIPS_ARCH}" )
endif ()
print_variables ( ARCH_AMD64 ARCH_X86 DYNAMIPS_ARCH )

# Compiler flags
foreach ( _flag
   -mdynamic-no-pic # Mac OS X
   )
   standard_variable_name ( _var "FLAG_${_flag}" )
   set_cmake_required ()
   check_c_compiler_flag ( ${_flag} ${_var} )
   if ( ${_var} )
      list ( APPEND DYNAMIPS_FLAGS ${_flag} )
   endif ()
endforeach ()

# librt (clock_gettime)
set_cmake_required ()
check_library_exists ( rt clock_gettime "time.h" USE_LIBRT )
if ( USE_LIBRT )
   list ( APPEND DYNAMIPS_LIBRARIES rt )
   print_variables ( USE_LIBRT )
endif ()

# libsocket (connect)
set_cmake_required ()
check_library_exists ( socket connect "sys/socket.h" USE_LIBSOCKET )
if ( USE_LIBSOCKET )
   list ( APPEND DYNAMIPS_LIBRARIES socket )
   print_variables ( USE_LIBSOCKET )
endif ()

# libnsl (gethostbyname)
set_cmake_required ()
check_library_exists ( nsl gethostbyname "netdb.h" USE_LIBNSL )
if ( USE_LIBNSL )
   list ( APPEND DYNAMIPS_LIBRARIES nsl )
   print_variables ( USE_LIBNSL )
endif ()

# libelf
set_cmake_required ()
find_package ( LibElf REQUIRED )
print_variables ( LIBELF_FOUND LIBELF_INCLUDE_DIRS LIBELF_LIBRARIES LIBELF_DEFINITIONS )
# make sure it can be used
set_cmake_required ()
list ( APPEND CMAKE_REQUIRED_DEFINITIONS ${LIBELF_DEFINITIONS} )
list ( APPEND CMAKE_REQUIRED_INCLUDES ${LIBELF_INCLUDE_DIRS} )
check_arch_library ( LIBELF_VALID elf_begin "libelf.h" LIBELF_LIBRARIES elf )
if ( NOT LIBELF_VALID )
   bad_arch_library ( FATAL_ERROR "libelf" "LIBELF_INCLUDE_DIRS and LIBELF_LIBRARIES" )
endif ()
list ( APPEND DYNAMIPS_DEFINITIONS ${LIBELF_DEFINITIONS} )
list ( APPEND DYNAMIPS_INCLUDES ${LIBELF_INCLUDE_DIRS} )
list ( APPEND DYNAMIPS_LIBRARIES ${LIBELF_LIBRARIES} )
# XXX some old libelf's aren't large file aware with ILP32
set ( _code "
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE64
#include <libelf.h>
int main() { return 0; }
" )
set_cmake_required ()
check_c_source_compiles ( "${_code}" LIBELF_LARGEFILE )
print_variables ( LIBELF_LARGEFILE )

# libuuid
set_cmake_required ()
find_package ( UUID REQUIRED )
print_variables ( UUID_FOUND UUID_INCLUDE_DIR UUID_LIBRARY )
# make sure it can be used
set_cmake_required ()
list ( APPEND CMAKE_REQUIRED_INCLUDES ${UUID_INCLUDE_DIR} )
check_arch_library ( UUID_VALID uuid_generate "uuid/uuid.h" UUID_LIBRARY uuid )
if ( NOT UUID_VALID )
   bad_arch_library ( FATAL_ERROR "uuid" "UUID_INCLUDE_DIR and UUID_LIBRARY" )
endif ()
list ( APPEND DYNAMIPS_INCLUDES ${UUID_INCLUDE_DIR} )
list ( APPEND DYNAMIPS_LIBRARIES ${UUID_LIBRARY} )

# pthreads
set ( CMAKE_THREAD_PREFER_PTHREAD 1 )
set_cmake_required ()
find_package ( Threads REQUIRED )
if ( CMAKE_USE_PTHREADS_INIT )
   list ( APPEND DYNAMIPS_LIBRARIES ${CMAKE_THREAD_LIBS_INIT} )
   print_variables ( CMAKE_THREAD_LIBS_INIT CMAKE_USE_PTHREADS_INIT )
   # ok
else ()
   print_variables ( CMAKE_THREAD_LIBS_INIT CMAKE_USE_SPROC_INIT 
      CMAKE_USE_WIN32_THREADS_INIT CMAKE_USE_PTHREADS_INIT 
      CMAKE_HP_PTHREADS_INIT )
   message ( FATAL_ERROR
      "Not using pthreads. "
      "The source assumes pthreads is available. "
      )
endif ()

# libpcap/winpcap (optional)
set_cmake_required ()
find_package ( PCAP )
print_variables ( PCAP_FOUND PCAP_INCLUDE_DIRS PCAP_LIBRARIES )
set ( HAVE_PCAP ${PCAP_FOUND} )
if ( HAVE_PCAP )
   # make sure it can be used
   set_cmake_required ()
   list ( APPEND CMAKE_REQUIRED_INCLUDES ${PCAP_INCLUDE_DIRS} )
   check_arch_library ( PCAP_VALID pcap_open_live "pcap.h" PCAP_LIBRARIES pcap wpcap )
   if ( NOT PCAP_VALID )
      bad_arch_library ( WARNING "pcap/wpcap" "PCAP_INCLUDE_DIRS and PCAP_LIBRARIES" )
   endif ()
   set ( HAVE_PCAP ${PCAP_VALID} )
endif ()
if ( HAVE_PCAP AND CYGWIN )
   # cygwin requires pcap_open
   set_cmake_required ()
   list ( APPEND CMAKE_REQUIRED_DEFINITIONS "-DHAVE_REMOTE" )
   list ( APPEND CMAKE_REQUIRED_INCLUDES ${PCAP_INCLUDE_DIRS} )
   list ( APPEND CMAKE_REQUIRED_LIBRARIES ${PCAP_LIBRARIES} )
   check_function_exists ( pcap_open HAVE_PCAP_OPEN )
   set ( HAVE_PCAP ${HAVE_PCAP_OPEN} )
endif ()
print_variables ( HAVE_PCAP )

# headers
# TODO minimize headers in the source
set ( _missing )
foreach ( _header #standalone
   "arpa/inet.h"
   "arpa/telnet.h" #dev_vtty.c
   "assert.h"
   "ctype.h"
   "dlfcn.h" #plugin.c
   "errno.h"
   "fcntl.h"
   "getopt.h" #dynamips.c
   "glob.h"
   #"libelf.h" #find_package
   #"linux/if.h" #linux_eth.c (LINUX_ETH)
   #"linux/if_packet.h" #linux_eth.c (LINUX_ETH)
   "netdb.h"
   #"netinet/if_ether.h" #linux_eth.c (LINUX_ETH)
   #"netinet/tcp.h" #dev_vtty.c
   #"pcap.h" #find_package (GEN_ETH)
   #"pthread.h" #find_package
   "setjmp.h"
   "signal.h"
   "stdarg.h"
   "stddef.h"
   "stdio.h"
   "stdlib.h"
   "string.h"
   "sys/ioctl.h"
   "sys/mman.h" #utils.c
   "sys/select.h"
   "sys/socket.h"
   "sys/stat.h"
   "sys/time.h"
   "sys/types.h"
   "sys/uio.h" #atm_vsar.c
   "sys/un.h"
   "sys/wait.h"
   "termios.h"
   "time.h"
   "unistd.h"
   #"uuid/uuid.h" #find_package
   )
   standard_variable_name ( _var "HAVE_${_header}" )
   set_cmake_required ()
   check_include_file ( "${_header}" ${_var} )
   if ( NOT "${${_var}}" )
      print_variables ( ${_var} )
      list ( APPEND _missing ${_header} )
   endif ()
endforeach ()
if ( HAVE_SYS_TYPES_H )
   foreach ( _header #requires sys/types.h
      "netinet/tcp.h"
      )
      standard_variable_name ( _var "HAVE_${_header}" )
      set_cmake_required ()
      check_include_files ( "sys/types.h;${_header}" ${_var} )
      if ( NOT "${${_var}}" )
         print_variables ( ${_var} )
         list ( APPEND _missing ${_header} )
      endif ()
   endforeach ()
endif ( HAVE_SYS_TYPES_H )
if ( _missing )
   message ( FATAL_ERROR "missing headers: ${_missing}" )
endif ( _missing )

# posix_memalign
check_function_exists ( posix_memalign HAVE_POSIX_MEMALIGN )
if ( HAVE_POSIX_MEMALIGN )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DHAS_POSIX_MEMALIGN=1" )
else ()
   list ( APPEND DYNAMIPS_DEFINITIONS "-DHAS_POSIX_MEMALIGN=0" )
endif ()
print_variables ( HAVE_POSIX_MEMALIGN )

# IPv6 (RFC 2553)
set ( _headers "sys/socket.h" "arpa/inet.h" "net/if.h" "netdb.h"
   "netinet/in.h" )
check_include_files ( "${_headers}" IPV6_HEADERS )
check_function_exists ( getaddrinfo HAVE_GETADDRINFO )
check_function_exists ( freeaddrinfo HAVE_FREEADDRINFO )
check_function_exists ( gai_strerror HAVE_GAI_STRERROR )
check_function_exists ( inet_pton HAVE_INET_PTON )
check_function_exists ( inet_ntop HAVE_INET_NTOP )
# TODO AF_INET6, PF_INET6, AI_PASSIVE, IPPROTO_IPV6, IPV6_JOIN_GROUP, 
#      IPV6_MULTICAST_HOPS, INET6_ADDRSTRLEN, struct sockaddr_storage, 
#      struct sockaddr_in6, struct ipv6_mreq
if ( IPV6_HEADERS AND HAVE_GETADDRINFO AND HAVE_FREEADDRINFO 
   AND HAVE_GAI_STRERROR AND HAVE_INET_PTON AND HAVE_INET_NTOP )
   set ( HAVE_IPV6 1 )
else ()
   set ( HAVE_IPV6 0 )
endif ()
print_variables ( HAVE_IPV6 )

message ( STATUS "dependencies - END" )
