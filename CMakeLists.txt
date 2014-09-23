# dynamips - build system
# Control variables:
#  - DYNAMIPS_RENAME : rename executable to dynamips (auto;stable;unstable;<empty>)
#  - DYNAMIPS_CODE   : set default code (stable;unstable;both)
#  - DYNAMIPS_ARCH   : build target architecture (amd64;x86;nojit)
#  - ANY_COMPILER    : skip compiler check (assumes gcc)
#  - CMAKE_INSTALL_PREFIX : where to install

set ( CMAKE_LEGACY_CYGWIN_WIN32 0 ) # Remove when CMake >= 2.8.4 is required
cmake_minimum_required ( VERSION 2.8 )
cmake_policy ( VERSION 2.8 )
cmake_policy ( SET CMP0017 NEW ) # Prefer files from the CMake module directory when including from there.
foreach ( _dir
   "/usr/sfw" # search in "Sun FreeWare" (SunOS)
   "/opt/csw" # search in "OpenCSW" (SunOS)
   )
   if ( IS_ABSOLUTE "${_dir}" AND IS_DIRECTORY "${_dir}" )
      list ( APPEND CMAKE_PREFIX_PATH "${_dir}" )
   endif ()
endforeach ()
if ( CMAKE_PREFIX_PATH )
   message ( STATUS "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}" )
endif ( CMAKE_PREFIX_PATH )
list ( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )
message ( STATUS "CMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}" )

project ( dynamips C )
set ( DYNAMIPS_VERSION_TRAIN 0.2.14 )
set ( DYNAMIPS_VERSION_SUB )

include ( utils )
include ( dependencies )
include ( configure )

add_subdirectory ( man )
add_subdirectory ( common )
add_subdirectory ( stable )
add_subdirectory ( unstable )

install_docs (
   "ChangeLog"
   "COPYING"
   "MAINTAINERS"
   "README"
   "README.hypervisor"
   "RELEASE-NOTES"
   "TODO"
   )

# uninstall target - leaves behind any directory created during install
set ( _templatefile "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in" )
set ( _scriptfile "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake" )
configure_file ( "${_templatefile}" "${_scriptfile}" IMMEDIATE @ONLY )
add_custom_target (
   uninstall
   COMMAND ${CMAKE_COMMAND} -P "${_scriptfile}"
   COMMENT "Uninstall files listed in install_manifest.txt"
   )

print_summary ()
