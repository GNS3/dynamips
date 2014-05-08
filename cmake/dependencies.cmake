# dynamips - check dependencies
# - libelf: rom2c, dynamips
# - libpcap: dynamips
# - libuuid: dynamips
# - libdl, librt, libsocket

# standard checks
include ( CheckFunctionExists )
include ( CheckLibraryExists )
include ( CheckCSourceCompiles )

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

# clock_gettime
check_function_exists ( clock_gettime HAVE_CLOCK_GETTIME_NO_LIB )
if ( NOT HAVE_CLOCK_GETTIME_NO_LIB )
   check_library_exists ( rt clock_gettime "time.h" HAVE_CLOCK_GETTIME_IN_RT )
   if ( HAVE_CLOCK_GETTIME_IN_RT )
      set ( USE_LIBRT 1 )
      message ( STATUS "USE_LIBRT=${USE_LIBRT}" )
   else ()
      message ( FATAL_ERROR "function clock_gettime is REQUIRED" )
   endif ()
endif ( NOT HAVE_CLOCK_GETTIME_NO_LIB )

# connect
check_function_exists ( connect HAVE_CONNECT_NO_LIB )
if ( NOT HAVE_CONNECT_NO_LIB )
   check_library_exists ( socket connect "sys/socket.h" HAVE_CONNECT_IN_SOCKET )
   if ( HAVE_CONNECT_IN_SOCKET )
      set ( USE_LIBSOCKET 1 )
      message ( STATUS "USE_LIBSOCKET=${USE_LIBSOCKET}" )
   else ()
      message ( FATAL_ERROR "function connect is REQUIRED" )
   endif ()
endif ( NOT HAVE_CONNECT_NO_LIB )

# gethostbyname
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

message ( STATUS "dependencies - END" )
