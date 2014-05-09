# dynamips - check dependencies
# - libelf: rom2c, dynamips
# - libpcap: dynamips
# - libuuid: dynamips
# - libdl, librt, libsocket

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

# libelf
find_package ( LibElf REQUIRED )
# XXX some old libelf's aren't large file aware
set ( CMAKE_REQUIRED_INCLUDES ${LIBELF_INCLUDE_DIRS} )
set ( CMAKE_REQUIRED_LIBRARIES ${LIBELF_LIBRARIES} )
set ( _code "
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE_SOURCE64
#include <libelf.h>
int main() { return 0; }
" )
check_c_source_compiles ( "${_code}" LIBELF_LARGEFILE )
print_variables (
   LIBELF_FOUND
   LIBELF_INCLUDE_DIRS
   LIBELF_LIBRARIES
   LIBELF_DEFINITIONS
   LIBELF_LARGEFILE
   )
include_directories ( ${LIBELF_INCLUDE_DIRS} )

# libuuid
find_package ( UUID REQUIRED )
print_variables (
   UUID_FOUND
   UUID_INCLUDE_DIR
   UUID_LIBRARY
   )
include_directories ( ${UUID_INCLUDE_DIR} )

# pthreads
set ( CMAKE_THREAD_PREFER_PTHREAD 1 )
find_package ( Threads REQUIRED )
if ( CMAKE_USE_PTHREADS_INIT )
   # ok
else ( CMAKE_USE_PTHREADS_INIT )
   print_variables (
      CMAKE_THREAD_LIBS_INIT
      CMAKE_USE_SPROC_INIT
      CMAKE_USE_WIN32_THREADS_INIT
      CMAKE_USE_PTHREADS_INIT
      CMAKE_HP_PTHREADS_INIT
      )
   message ( FATAL_ERROR
      "Not using pthreads. "
      "The source assumes pthreads is available. "
      )
endif ( CMAKE_USE_PTHREADS_INIT )

# libpcap/winpcap (optional)
find_package ( PCAP )
if ( PCAP_FOUND )
   include_directories ( ${PCAP_INCLUDE_DIRS} )
endif ( PCAP_FOUND )

# general headers
# TODO minimize headers in the source
set ( MISSING_HEADERS )
set ( _headers #standalone
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
   "malloc.h" #utils.c
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
check_headers_exist ( MISSING_HEADERS ${_headers} )
if ( HAVE_SYS_TYPES_H )
   set ( _headers #depend on sys/types.h
      "netinet/tcp.h"
      )
   check_dependent_headers_exist ( MISSING_HEADERS "sys/types.h" ${_headers} )
endif ( HAVE_SYS_TYPES_H )
if ( MISSING_HEADERS )
   message ( FATAL_ERROR "missing headers: ${MISSING_HEADERS}" )
endif ( MISSING_HEADERS )

# librt (clock_gettime)
check_library_exists ( rt clock_gettime "time.h" HAVE_CLOCK_GETTIME_IN_RT )
if ( HAVE_CLOCK_GETTIME_IN_RT )
   set ( USE_LIBRT 1 )
   print_variables ( USE_LIBRT )
endif ()

# libsocket (connect)
check_function_exists ( connect HAVE_CONNECT_NO_LIB )
if ( NOT HAVE_CONNECT_NO_LIB )
   check_library_exists ( socket connect "sys/socket.h" HAVE_CONNECT_IN_SOCKET )
   if ( HAVE_CONNECT_IN_SOCKET )
      set ( USE_LIBSOCKET 1 )
      print_variables ( USE_LIBSOCKET )
   else ()
      message ( FATAL_ERROR "function connect is REQUIRED" )
   endif ()
endif ( NOT HAVE_CONNECT_NO_LIB )

# libnsl (gethostbyname)
check_function_exists ( gethostbyname HAVE_GETHOSTBYNAME_NO_LIB )
if ( NOT HAVE_GETHOSTBYNAME_NO_LIB )
   check_library_exists ( nsl gethostbyname "netdb.h" HAVE_CONNECT_IN_NSL )
   if ( HAVE_CONNECT_IN_SOCKET )
      set ( USE_LIBNSL 1 )
      print_variables ( USE_LIBNSL )
   else ()
      message ( FATAL_ERROR "function gethostbyname is REQUIRED" )
   endif ()
endif ( NOT HAVE_GETHOSTBYNAME_NO_LIB )

# posix_memalign
check_function_exists ( posix_memalign HAVE_POSIX_MEMALIGN )
print_variables ( HAVE_POSIX_MEMALIGN )

# IPv6
check_function_exists ( getnameinfo HAVE_GETNAMEINFO )
# TODO AF_INET6, PF_INET6, struct sockaddr_in6, struct sockaddr_storage?

message ( STATUS "dependencies - END" )
