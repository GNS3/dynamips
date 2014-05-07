# dynamips - check dependencies
# - libelf: rom2c, dynamips
# - libpcap: dynamips
# - libuuid: dynamips
# - libdl: dynamips
# - librt: dynamips

message ( STATUS "dependencies - BEGIN" )

include(CheckFunctionExists)
include(CheckLibraryExists)

# gnu compiler
if ( NOT CMAKE_COMPILER_IS_GNUCC AND NOT ANY_COMPILER )
   message ( STATUS "CMAKE_COMPILER_IS_GNUCC=${CMAKE_COMPILER_IS_GNUCC}" )
   message ( STATUS "CMAKE_C_COMPILER=${CMAKE_C_COMPILER}" )
   message ( STATUS "CMAKE_C_COMPILER_ABI=${CMAKE_C_COMPILER_ABI}" )
   message ( STATUS "CMAKE_C_COMPILER_ID=${CMAKE_C_COMPILER_ID}" )
   message ( STATUS "CMAKE_C_COMPILER_VERSION=${CMAKE_C_COMPILER_VERSION}" )
   message ( FATAL_ERROR
      "Not a GNU C compiler. "
      "The source and build system assumes gcc so it might not compile. "
      "Invoke cmake with -DANY_COMPILER=1 to skip this check. "
      )
endif ( NOT CMAKE_COMPILER_IS_GNUCC AND NOT ANY_COMPILER )

# libelf
find_package ( LibElf REQUIRED )
message ( STATUS "LIBELF_FOUND=${LIBELF_FOUND}" )
message ( STATUS "LIBELF_INCLUDE_DIRS=${LIBELF_INCLUDE_DIRS}" )
message ( STATUS "LIBELF_LIBRARIES=${LIBELF_LIBRARIES}" )
message ( STATUS "LIBELF_DEFINITIONS=${LIBELF_DEFINITIONS}" )
include_directories ( ${LIBELF_INCLUDE_DIRS} )

# libuuid
find_package ( LibUUID REQUIRED )
message ( STATUS "LIBUUID_FOUND=${LIBUUID_FOUND}" )
message ( STATUS "LIBUUID_INCLUDE_DIRS=${LIBUUID_INCLUDE_DIRS}" )
message ( STATUS "LIBUUID_LIBRARIES=${LIBUUID_LIBRARIES}" )
include_directories ( ${LIBUUID_INCLUDE_DIRS} )

# pthreads
set ( CMAKE_THREAD_PREFER_PTHREAD 1 )
find_package ( Threads REQUIRED )
if ( CMAKE_USE_PTHREADS_INIT )
   # ok
else ( CMAKE_USE_PTHREADS_INIT )
   message ( STATUS "CMAKE_THREAD_LIBS_INIT=${CMAKE_THREAD_LIBS_INIT}" )
   message ( STATUS "CMAKE_USE_SPROC_INIT=${CMAKE_USE_SPROC_INIT}" )
   message ( STATUS "CMAKE_USE_WIN32_THREADS_INIT=${CMAKE_USE_WIN32_THREADS_INIT}" )
   message ( STATUS "CMAKE_USE_PTHREADS_INIT=${CMAKE_USE_PTHREADS_INIT}" )
   message ( STATUS "CMAKE_HP_PTHREADS_INIT=${CMAKE_HP_PTHREADS_INIT}" )
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
      message ( STATUS "USE_LIBNSL=${USE_LIBNSL}" )
   else ()
      message ( FATAL_ERROR "function gethostbyname is REQUIRED" )
   endif ()
endif ( NOT HAVE_GETHOSTBYNAME_NO_LIB )

# posix_memalign
check_function_exists ( posix_memalign HAVE_POSIX_MEMALIGN )
message ( STATUS "HAVE_POSIX_MEMALIGN=${HAVE_POSIX_MEMALIGN}" )

message ( STATUS "dependencies - END" )
