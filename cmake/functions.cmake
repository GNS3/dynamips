# dynamips - auxiliary functions

# add compile flags to a target
function ( target_add_compile_flags _target )
   if ( ${ARGC} LESS 2 )
      # no definitions
      return ()
   endif ()
   get_target_property ( _flags ${_target} COMPILE_FLAGS )
   if ( NOT _flags )
      set ( _flags "" )
   endif ()
   list ( REMOVE_AT ARGV 0 ) # remove _target
   foreach ( _flag ${ARGV} )
      string ( STRIP "${_flags} ${_flag}" _flags )
   endforeach ()
   set_target_properties ( ${_target} PROPERTIES COMPILE_FLAGS ${_flags} )
endfunction ( target_add_compile_flags _target )

# print configuration summary
function ( print_summary )
   message ( "Summary:" )
   if ( DEFINED ENABLE_LINUX_ETH )
      set ( _status "${ENABLE_LINUX_ETH} (ENABLE_LINUX_ETH)" )
   else ()
      set ( _status "DISABLED (not Linux)" )
   endif ()
   message ( "\tRAW Ethernet support for Linux: ${_status}" )
   if ( DEFINED ENABLE_GEN_ETH )
      set ( _status "${ENABLE_GEN_ETH} (ENABLE_GEN_ETH)" )
   else ()
      set ( _status "DISABLED (library not found)" )
   endif ()
   message ( "\tGeneric Ethernet support with libpcap (0.9+) or winpcap: ${_status}" )
   message ( "\tCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}" )
endfunction ( print_summary )

# install executables
function ( install_executables )
   if ( ${ARGC} LESS 1 )
      # no executables
      return ()
   endif ()
   install (
      TARGETS ${ARGV}
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT "executables"
      )
endfunction ( install_executables )

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
