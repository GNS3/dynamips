# dynamips - configure

message ( STATUS "configure - BEGIN" )

if ( LIBELF_LARGEFILE )
   option ( ENABLE_LARGEFILE "compile with large file support" ON )
endif ( LIBELF_LARGEFILE )
if ( ENABLE_LARGEFILE )
   add_definitions (
      "-D_FILE_OFFSET_BITS=64"
      "-D_LARGEFILE_SOURCE"
      "-D_LARGEFILE64_SOURCE"
      )
endif ( ENABLE_LARGEFILE )
add_definitions (
   "-DHAS_POSIX_MEMALIGN=${HAVE_POSIX_MEMALIGN}"
   )
set ( DYNAMIPS_FLAGS -Wall -O2 -fomit-frame-pointer )
set ( DYNAMIPS_LIBRARIES ${LIBELF_LIBRARIES} ${UUID_LIBRARY} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_DL_LIBS} )
if ( USE_LIBRT )
   set ( DYNAMIPS_LIBRARIES "-lrt" ${DYNAMIPS_LIBRARIES} )
endif ( USE_LIBRT )
if ( USE_LIBSOCKET )
   set ( DYNAMIPS_LIBRARIES "-lsocket" ${DYNAMIPS_LIBRARIES} )
endif ( USE_LIBSOCKET )
if ( USE_LIBNSL )
   set ( DYNAMIPS_LIBRARIES "-lnsl" ${DYNAMIPS_LIBRARIES} )
endif ( USE_LIBNSL )
if ( APPLE )
   # TODO test compiler flag
   set ( DYNAMIPS_FLAGS ${DYNAMIPS_FLAGS} -mdynamic-no-pic )
endif ( APPLE )
if ( "${CMAKE_SYSTEM_NAME}" STREQUAL "SunOS" )
   add_definitions ( "-DSUNOS" "-DINADDR_NONE=0xFFFFFFFF" )
endif ()
if ( CYGWIN )
   set ( DYNAMIPS_FLAGS ${DYNAMIPS_FLAGS} -DCYGWIN )
endif ( CYGWIN )

include ( GNUInstallDirs )

# DYNAMIPS_VERSION
set ( DYNAMIPS_VERSION "\"${DYNAMIPS_VERSION_TRAIN}${DYNAMIPS_VERSION_SUB}\"" )
add_definitions ( "-DDYNAMIPS_VERSION=${DYNAMIPS_VERSION}" )
message ( STATUS "DYNAMIPS_VERSION=${DYNAMIPS_VERSION}" )

# Target code:
#  - "stable"
#  - "unstable"
if ( APPLE )
   set ( DYNAMIPS_CODE_DEFAULT "unstable" )
else ()
   set ( DYNAMIPS_CODE_DEFAULT "stable" )
endif ()
set ( _stable_default OFF )
set ( _unstable_default OFF )
if ( NOT DYNAMIPS_CODE )
   set ( DYNAMIPS_CODE "${DYNAMIPS_CODE_DEFAULT}" )
endif ( NOT DYNAMIPS_CODE )
set ( DYNAMIPS_CODE "${DYNAMIPS_CODE}" CACHE STRING "stable,unstable,both" )
set_property ( CACHE DYNAMIPS_CODE PROPERTY STRINGS "stable" "unstable" "both" )
if ( "${DYNAMIPS_CODE}" STREQUAL "both" )
   set ( _stable_default ON )
   set ( _unstable_default ON )
elseif ( "${DYNAMIPS_CODE}" STREQUAL "stable" )
   set ( _stable_default ON )
elseif ( "${DYNAMIPS_CODE}" STREQUAL "unstable" )
   set ( _unstable_default ON )
endif ()
option ( BUILD_DYNAMIPS_STABLE "build the stable version of the dynamips executable" ${_stable_default} )
option ( BUILD_DYNAMIPS_UNSTABLE "build the unstable version of the dynamips executable" ${_unstable_default} )
message ( STATUS "DYNAMIPS_CODE=${DYNAMIPS_CODE}" )
message ( STATUS "BUILD_DYNAMIPS_STABLE=${BUILD_DYNAMIPS_STABLE}" )
message ( STATUS "BUILD_DYNAMIPS_UNSTABLE=${BUILD_DYNAMIPS_UNSTABLE}" )

# Target architecture:
#  - Use "x86" to build for x86 (32-bit)
#  - Use "amd64" to build for x86_64 (64-bit)
#  - Use "nojit" to build for other architectures (no recompilation)
if ( NOT DYNAMIPS_ARCH )
   # XXX maybe use CMAKE_SYSTEM_PROCESSOR?
   include ( TargetArch )
   target_architecture ( DYNAMIPS_ARCH )
   if ( "${DYNAMIPS_ARCH}" STREQUAL "i386" )
      set ( DYNAMIPS_ARCH "x86" )
   elseif ( "${DYNAMIPS_ARCH}" STREQUAL "x86_64" )
      set ( DYNAMIPS_ARCH "amd64" )
   else ()
      message ( STATUS "detected DYNAMIPS_ARCH=${DYNAMIPS_ARCH}" )
      set ( DYNAMIPS_ARCH "nojit" )
   endif ()
endif ( NOT DYNAMIPS_ARCH )
set ( DYNAMIPS_ARCH "${DYNAMIPS_ARCH}" CACHE STRING "x86,amd64,nojit" )
set_property ( CACHE DYNAMIPS_ARCH PROPERTY STRINGS "x86" "amd64" "nojit" )
set ( JIT_ARCH "\"${DYNAMIPS_ARCH}\"" )
set ( JIT_CPU "${DYNAMIPS_ARCH}" )
set ( MIPS64_ARCH_INC_FILE "\"mips64_${DYNAMIPS_ARCH}_trans.h\"" )
set ( PPC32_ARCH_INC_FILE "\"ppc32_${DYNAMIPS_ARCH}_trans.h\"" )
add_definitions ( "-DJIT_ARCH=${JIT_ARCH}" )
add_definitions ( "-DJIT_CPU=${JIT_CPU}" )
add_definitions ( "-DMIPS64_ARCH_INC_FILE=${MIPS64_ARCH_INC_FILE}" )
add_definitions ( "-DPPC32_ARCH_INC_FILE=${PPC32_ARCH_INC_FILE}" )
message ( STATUS "DYNAMIPS_ARCH=${DYNAMIPS_ARCH}" )
message ( STATUS "JIT_ARCH=${JIT_ARCH}" )
message ( STATUS "JIT_CPU=${JIT_CPU}" )
message ( STATUS "MIPS64_ARCH_INC_FILE=${MIPS64_ARCH_INC_FILE}" )
message ( STATUS "PPC32_ARCH_INC_FILE=${PPC32_ARCH_INC_FILE}" )

# other executables
option ( BUILD_NVRAM_EXPORT "build the nvram_export executable" ON )
option ( BUILD_UDP_SEND "build the udp_send executable" OFF )
option ( BUILD_UDP_RECV "build the udp_recv executable" OFF )

# OSNAME
if ( NOT OSNAME )
   if ( "${CMAKE_SYSTEM_NAME}" STREQUAL "CYGWIN" )
      # XXX maybe keep it as CYGWIN?
      set ( OSNAME "Windows" )
   elseif ( CMAKE_SYSTEM_NAME )
      set ( OSNAME "${CMAKE_SYSTEM_NAME}" )
   else ()
      message ( STATUS "CMAKE_SYSTEM=${CMAKE_SYSTEM}" )
      message ( STATUS "CMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}" )
      message ( STATUS "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}" )
      message ( STATUS "CMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}" )
      set ( OSNAME "unknown" )
   endif ()
endif ( NOT OSNAME )
mark_as_advanced ( OSNAME )
message ( STATUS "OSNAME=${OSNAME}" )

# LINUX_ETH
if ( "${CMAKE_SYSTEM_NAME}" STREQUAL "Linux" )
   option ( ENABLE_LINUX_ETH "RAW Ethernet support for Linux" ON )
   message ( STATUS "ENABLE_LINUX_ETH=${ENABLE_LINUX_ETH}" )
endif ()

# GEN_ETH
if ( PCAP_FOUND )
   option ( ENABLE_GEN_ETH "Generic Ethernet support with libpcap (0.9+) or winpcap" ON )
   message ( STATUS "ENABLE_GEN_ETH=${ENABLE_GEN_ETH}" )
endif ()

message ( STATUS "configure - END" )
