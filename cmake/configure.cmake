# dynamips - configure options
#  - DYNAMIPS_ARCH
#  - DYNAMIPS_CODE
#  - BUILD_NVRAM_EXPORT
#  - BUILD_UDP_SEND (default OFF)
#  - BUILD_UDP_RECV (default OFF)
#  - ENABLE_LARGEFILE
#  - ENABLE_LINUX_ETH
#  - ENABLE_GEN_ETH
#  - ENABLE_IPV6
# accumulators:
#  - DYNAMIPS_FLAGS
#  - DYNAMIPS_DEFINITIONS
#  - DYNAMIPS_INCLUDES
#  - DYNAMIPS_LIBRARIES
# XXX assumes utils.cmake and dependencies.cmake were included

message ( STATUS "configure - BEGIN" )

# DYNAMIPS_VERSION
set ( DYNAMIPS_VERSION "\"${DYNAMIPS_VERSION_TRAIN}${DYNAMIPS_VERSION_SUB}\"" )
list ( APPEND DYNAMIPS_DEFINITIONS "-DDYNAMIPS_VERSION=${DYNAMIPS_VERSION}" )
message ( STATUS "DYNAMIPS_VERSION=${DYNAMIPS_VERSION}" )

# Target architecture: (set in dependencies file)
#  - Use "x86" to build for x86 (32-bit)
#  - Use "amd64" to build for x86_64 (64-bit)
#  - Use "nojit" to build for other architectures (no recompilation)
set ( JIT_ARCH "\"${DYNAMIPS_ARCH}\"" )
set ( JIT_CPU "CPU_${DYNAMIPS_ARCH}" )
set ( MIPS64_ARCH_INC_FILE "\"mips64_${DYNAMIPS_ARCH}_trans.h\"" )
set ( PPC32_ARCH_INC_FILE "\"ppc32_${DYNAMIPS_ARCH}_trans.h\"" )
list ( APPEND DYNAMIPS_DEFINITIONS "-DJIT_ARCH=${JIT_ARCH}" "-DJIT_CPU=${JIT_CPU}"
   "-DMIPS64_ARCH_INC_FILE=${MIPS64_ARCH_INC_FILE}"
   "-DPPC32_ARCH_INC_FILE=${PPC32_ARCH_INC_FILE}" )
if ( APPLE AND "amd64" STREQUAL "${DYNAMIPS_ARCH}" )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DMAC64HACK" )
endif()
print_variables ( DYNAMIPS_ARCH )

# Target code:
#  - "stable"
#  - "unstable"
#  - "both" (stable + unstable)
#  - "none"
if ( APPLE )
   set ( _default "unstable" )
else ()
   set ( _default "stable" )
endif ()
set ( DYNAMIPS_CODE "${_default}" CACHE STRING "Target code (stable;unstable;both;none)" )
set_property ( CACHE DYNAMIPS_ARCH PROPERTY STRINGS "stable" "unstable" "both" "none" )
if ( NOT DYNAMIPS_CODE )
   set ( DYNAMIPS_CODE "${_default}" )
endif () 
if ( "stable" STREQUAL DYNAMIPS_CODE )
   set ( BUILD_DYNAMIPS_STABLE ON )
   set ( BUILD_DYNAMIPS_UNSTABLE OFF )
elseif ( "unstable" STREQUAL DYNAMIPS_CODE )
   set ( BUILD_DYNAMIPS_STABLE OFF )
   set ( BUILD_DYNAMIPS_UNSTABLE ON )
elseif ( "both" STREQUAL DYNAMIPS_CODE )
   set ( BUILD_DYNAMIPS_STABLE ON )
   set ( BUILD_DYNAMIPS_UNSTABLE ON )
elseif ( "none" STREQUAL DYNAMIPS_CODE )
   set ( BUILD_DYNAMIPS_STABLE OFF )
   set ( BUILD_DYNAMIPS_UNSTABLE OFF )
else ()
   message ( FATAL_ERROR "unknown target code DYNAMIPS_CODE=${DYNAMIPS_CODE} (stable;unstable;both;none)" )
endif ()
print_variables ( DYNAMIPS_CODE BUILD_DYNAMIPS_STABLE BUILD_DYNAMIPS_UNSTABLE )

# Rename target (auto;stable;unstable;<empty>)
# XXX should auto or not renaming be the default?
set ( DYNAMIPS_RENAME "auto" CACHE STRING "which executable is renamed to dynamips (auto;stable;unstable;<empty>)" )
set_property ( CACHE DYNAMIPS_RENAME PROPERTY STRINGS "auto" "stable" "unstable" )
set ( DYNAMIPS_RENAME_TARGET )
if ( "auto" STREQUAL DYNAMIPS_RENAME )
   foreach ( _target "${DYNAMIPS_CODE}" "stable" "unstable" )
      string ( TOUPPER "BUILD_DYNAMIPS_${_target}" _var )
      if ( ${_var} )
         set ( DYNAMIPS_RENAME_TARGET "dynamips_${DYNAMIPS_ARCH}_${_target}" )
         break ()
      endif ()
   endforeach ()
elseif ( "stable" STREQUAL DYNAMIPS_RENAME OR "unstable" STREQUAL DYNAMIPS_RENAME )
   set ( DYNAMIPS_RENAME_TARGET "dynamips_${DYNAMIPS_ARCH}_${DYNAMIPS_RENAME}" )
elseif ( DYNAMIPS_RENAME )
   message ( FATAL_ERROR "unknown rename target DYNAMIPS_RENAME=${DYNAMIPS_RENAME} (auto;stable;unstable;<empty>)" )
endif ()
print_variables ( DYNAMIPS_RENAME DYNAMIPS_RENAME_TARGET )

# other executables
option ( BUILD_NVRAM_EXPORT "build the nvram_export executable" ON )
option ( BUILD_UDP_SEND "build the udp_send executable" OFF )
option ( BUILD_UDP_RECV "build the udp_recv executable" OFF )
print_variables ( BUILD_NVRAM_EXPORT BUILD_UDP_SEND BUILD_UDP_RECV )

# ENABLE_LARGEFILE
if ( LIBELF_LARGEFILE )
   option ( ENABLE_LARGEFILE "compile with large file support" ON )
endif ()
if ( ENABLE_LARGEFILE )
   list ( APPEND DYNAMIPS_DEFINITIONS "-D_FILE_OFFSET_BITS=64"
      "-D_LARGEFILE_SOURCE" "-D_LARGEFILE64_SOURCE" )
endif ( ENABLE_LARGEFILE )

# ENABLE_LINUX_ETH
if ( "Linux" STREQUAL "${CMAKE_SYSTEM_NAME}" )
   option ( ENABLE_LINUX_ETH "Linux ethernet support with RAW sockets (linux_eth)" ON )
   print_variables ( ENABLE_LINUX_ETH )
endif ()
if ( ENABLE_LINUX_ETH )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DLINUX_ETH" )
endif ()

# ENABLE_GEN_ETH
if ( HAVE_PCAP )
   option ( ENABLE_GEN_ETH "Generic Ethernet support with libpcap/winpcap (gen_eth)" ON )
   print_variables ( ENABLE_GEN_ETH )
endif ()
if ( ENABLE_GEN_ETH )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DGEN_ETH" )
   list ( APPEND DYNAMIPS_LIBRARIES ${PCAP_LIBRARIES} )
endif ()

# ENABLE_IPV6
if ( HAVE_IPV6 )
   option ( ENABLE_IPV6 "IPv6 support  (RFC 2553)" ON )
   print_variables ( ENABLE_IPV6 )
endif ()
if ( ENABLE_IPV6 )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DHAS_RFC2553=1" )
else ()
   list ( APPEND DYNAMIPS_DEFINITIONS "-DHAS_RFC2553=0" )
endif ()

# target system
if ( "SunOS" STREQUAL "${CMAKE_SYSTEM_NAME}" )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DSUNOS" "-DINADDR_NONE=0xFFFFFFFF" )
endif ()
if ( CYGWIN )
   list ( APPEND DYNAMIPS_DEFINITIONS "-DCYGWIN" )
endif ()
if ( "CYGWIN" STREQUAL "${CMAKE_SYSTEM_NAME}" )
   # XXX maybe keep it as CYGWIN?
   set ( OSNAME "Windows" )
elseif ( CMAKE_SYSTEM_NAME )
   set ( OSNAME "${CMAKE_SYSTEM_NAME}" )
else ()
   print_variables ( CMAKE_SYSTEM CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR 
      CMAKE_SYSTEM_VERSION )
   message ( WARNING "missing system name" )
   set ( OSNAME "unknown" )
endif ()
list ( APPEND DYNAMIPS_DEFINITIONS "-DOSNAME=${OSNAME}" )

# final setup
include ( GNUInstallDirs )
set ( CMAKE_C_FLAGS )
foreach ( _flag ${DYNAMIPS_FLAGS} )
   set ( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_flag}" )
endforeach ()
string ( STRIP "${CMAKE_C_FLAGS}" CMAKE_C_FLAGS )
add_definitions ( ${DYNAMIPS_DEFINITIONS} )
include_directories ( ${DYNAMIPS_INCLUDES} )
print_variables ( DYNAMIPS_FLAGS DYNAMIPS_DEFINITIONS DYNAMIPS_INCLUDES DYNAMIPS_LIBRARIES )

# summary
macro ( print_summary )
   message ( "Summary:" )
   message ( "  CMAKE_INSTALL_PREFIX               : ${CMAKE_INSTALL_PREFIX}" )
   message ( "  DYNAMIPS_ARCH                      : ${DYNAMIPS_ARCH}" )
   message ( "  DYNAMIPS_CODE                      : ${DYNAMIPS_CODE}" )
   if ( DYNAMIPS_RENAME_TARGET )
      set ( _rename "${DYNAMIPS_RENAME_TARGET} -> dynamips" )
   else ()
      set ( _rename "don't rename" )
   endif ()
   message ( "  DYNAMIPS_RENAME                    : ${_rename}  (${DYNAMIPS_RENAME})" )
   message ( "  BUILD_NVRAM_EXPORT                 : ${BUILD_NVRAM_EXPORT}" )
   message ( "  BUILD_UDP_SEND                     : ${BUILD_UDP_SEND}" )
   message ( "  BUILD_UDP_RECV                     : ${BUILD_UDP_RECV}" )
   if ( DEFINED ENABLE_LARGEFILE )
      set ( _largefile "ENABLE_LARGEFILE=${ENABLE_LARGEFILE}" )
   else ()
      set ( _largefile "libelf is incompatible" )
   endif ()
   message ( "  Large File support                 : ${_largefile}" )
   if ( DEFINED ENABLE_LINUX_ETH )
      set ( _linux_eth "ENABLE_LINUX_ETH=${ENABLE_LINUX_ETH}" )
   else ()
      set ( _linux_eth "no, not Linux" )
   endif ()
   message ( "  Linux Ethernet (RAW sockets)       : ${_linux_eth}  (linux_eth)" )
   if ( DEFINED ENABLE_GEN_ETH )
      set ( _gen_eth "ENABLE_GEN_ETH=${ENABLE_GEN_ETH}" )
   else ()
      set ( _gen_eth "libpcap/winpcap not found" )
   endif ()
   message ( "  Generic Ethernet (libpcap/WinPcap) : ${_gen_eth}  (gen_eth)" )
   if ( DEFINED ENABLE_IPV6 )
      set ( _ipv6 "ENABLE_IPV6=${ENABLE_IPV6}" )
   else ()
      set ( _ipv6 "no, missing headers or functions" )
   endif ()
   message ( "  IPv6 support (RFC 2553)            : ${_ipv6}" )
endmacro ( print_summary )

message ( STATUS "configure - END" )
