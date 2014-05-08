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

# print variables
function ( print_variables )
   foreach ( _var ${ARGV} )
      message ( STATUS "${_var}=${${_var}}" )
   endforeach ()
endfunction ( print_variables )

# print configuration summary
function ( print_summary )
   message ( "Summary:" )
   if ( DEFINED ENABLE_LINUX_ETH )
      set ( _linux_eth "ENABLE_LINUX_ETH=${ENABLE_LINUX_ETH} (linux_eth)" )
   else ()
      set ( _linux_eth "not Linux" )
   endif ()
   if ( DEFINED ENABLE_GEN_ETH )
      set ( _gen_eth "ENABLE_GEN_ETH=${ENABLE_GEN_ETH} (gen_eth)" )
   else ()
      set ( _gen_eth "libpcap/winpcap not found" )
   endif ()
   if ( DEFINED ENABLE_LARGEFILE )
      set ( _largefile "ENABLE_LARGEFILE=${ENABLE_LARGEFILE}" )
   else ()
      set ( _largefile "libelf is incompatible" )
   endif ()
   message ( "  Linux Ethernet (RAW sockets)       : ${_linux_eth}" )
   message ( "  Generic Ethernet (libpcap/WinPcap) : ${_gen_eth}" )
   message ( "  Large File support                 : ${_largefile}" )
   message ( "  CMAKE_INSTALL_PREFIX               : ${CMAKE_INSTALL_PREFIX}" )
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
