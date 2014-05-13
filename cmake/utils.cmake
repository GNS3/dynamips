# dynamips - utility functions

# standard checks
include ( CheckCCompilerFlag )
include ( CheckCSourceCompiles )
include ( CheckCSourceRuns )
include ( CheckCXXCompilerFlag )
include ( CheckCXXSourceCompiles )
include ( CheckCXXSourceRuns )
include ( CheckFunctionExists )
include ( CheckIncludeFile )
include ( CheckIncludeFileCXX )
include ( CheckIncludeFiles )
include ( CheckLibraryExists )
include ( CheckPrototypeDefinition )
include ( CheckStructHasMember )
include ( CheckSymbolExists )
include ( CheckTypeSize )
include ( CheckVariableExists )

# print variables
function ( print_variables )
   foreach ( _var ${ARGV} )
      message ( STATUS "${_var}=${${_var}}" )
   endforeach ()
endfunction ( print_variables )

# convert the name to a standard variable name
macro ( standard_variable_name _var _name )
      string ( REGEX REPLACE "[^a-zA-Z0-9]" "_" ${_var} "${_name}" )
      string ( TOUPPER "${${_var}}" ${_var} )
endmacro ()

# test if all the headers exist (also defines HAVE_<HEADER> variables)
macro ( check_headers_exist _missingvar )
   foreach ( _header ${ARGN} )
      standard_variable_name ( _var "HAVE_${_header}" )
      check_include_file ( ${_header} ${_var} )
      if ( NOT "${${_var}}" )
         print_variables ( ${_var} )
         list ( APPEND ${_missingvar} ${_header} )
      endif ( NOT "${${_var}}" )
   endforeach ( _header ${ARGN} )
endmacro ( check_headers_exist _missingvar )

# test if all the headers exist (also defines HAVE_<HEADER> variables)
macro ( check_dependent_headers_exist _missingvar _dependencies )
   foreach ( _header ${ARGN} )
      string ( REGEX REPLACE "[^a-zA-Z0-9]" "_" _var "HAVE_${_header}" )
      string ( TOUPPER "${_var}" _var )
      check_include_files ( "${_dependencies};${_header}" ${_var} )
      if ( NOT "${${_var}}" )
         print_variables ( ${_var} )
         list ( APPEND ${_missingvar} ${_header} )
      endif ( NOT "${${_var}}" )
   endforeach ( _header ${ARGN} )
endmacro ( check_dependent_headers_exist _missingvar _dependencies )

# rename target DYNAMIPS_RENAME_TARGET to dynamips
macro ( maybe_rename_to_dynamips _target )
   if ( "${_target}" STREQUAL "${DYNAMIPS_RENAME_TARGET}" )
      set_target_properties ( ${_target} PROPERTIES OUTPUT_NAME "dynamips" )
   endif()
endmacro ( maybe_rename_to_dynamips _target )

# install executables
function ( install_executable _target )
   install (
      TARGETS ${_target}
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT "executables"
      )
endfunction ( install_executable )

# install docs
function ( install_docs )
   if ( ${ARGC} LESS 1 )
     # no docs
     return ()
   endif ()
   install (
      FILES ${ARGV}
      DESTINATION "${CMAKE_INSTALL_DOCDIR}"
      COMPONENT "docs"
      )
endfunction ( install_docs )

# install man pages
function ( install_man_pages )
   if ( ${ARGC} LESS 1 )
     # no man pages
     return ()
   endif ()
   foreach ( _file ${ARGV} )
      string ( REGEX REPLACE "^.*\\." "" _page "${_file}" )
      if ( NOT "${_page}" MATCHES "[0-9]+" )
         message ( FATAL_ERROR "not a man page: ${_file}" )
      endif ()
      install (
         FILES "${_file}"
         DESTINATION "${CMAKE_INSTALL_MANDIR}/man${_page}"
         COMPONENT "docs"
         )
   endforeach ()
endfunction ( install_man_pages )
