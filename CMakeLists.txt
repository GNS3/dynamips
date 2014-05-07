set ( CMAKE_LEGACY_CYGWIN_WIN32 0 ) # Remove when CMake >= 2.8.4 is required
cmake_minimum_required ( VERSION 2.8 )
cmake_policy ( VERSION 2.8 )
cmake_policy ( SET CMP0017 NEW ) # Prefer files from the CMake module directory when including from there.

project ( dynamips )
set ( DYNAMIPS_VERSION_TRAIN 0.2.13 )
set ( DYNAMIPS_VERSION_SUB -dev )

set ( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake" )
include ( functions )
include ( dependencies )
include ( configure )

add_subdirectory ( man )
add_subdirectory ( common )
add_subdirectory ( stable )
add_subdirectory ( unstable )

# default dynamips target
add_custom_target ( dynamips
   DEPENDS dynamips_${DYNAMIPS_ARCH}_${DYNAMIPS_CODE_DEFAULT}
   COMMENT "default dynamips target (dynamips_${DYNAMIPS_ARCH}_${DYNAMIPS_CODE_DEFAULT})"
   )

install_docs (
   "ChangeLog"
   "COPYING"
   "MAINTAINERS"
   "README"
   "README.hypervisor"
   "RELEASE-NOTES"
   "TODO"
   )

print_summary ()
