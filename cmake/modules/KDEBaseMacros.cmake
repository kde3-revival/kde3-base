#[=======================================================================[.rst:
KDEBaseMacros
-------------

Install hooks of the kdebase Makefile.am files that the KDE3 package of
kdelibs has no function for.

``kdebase_install_pam_service(<service>)``
  ``install-data-local: -@test -n "$(DESTDIR)" || test -z "$(PAM)" ||
  $(top_srcdir)/mkpamserv $(PAM)``: create the PAM service definition
  with mkpamserv when installing without DESTDIR and PAM is used.
``kdebase_install_time_path(<var> <dir>)``
  A directory of the installation for install(CODE).
``kdebase_install_setgid(<program> <group> [<line>...])``
  ``install-exec-hook: (chown root:<group> $(bindir)/<program> && chmod
  2755 ...) || echo "Error: ..." <line>...``.
#]=======================================================================]

include_guard(GLOBAL)

# kdebase_install_time_path(<var> <dir>): <dir> (relative to the prefix
# or absolute) as it is to be written into install(CODE): relative ones
# below the ${CMAKE_INSTALL_PREFIX} of the installation, which
# "cmake --install --prefix" may change.  The same as kdelibs' private
# _kde3_install_time_path().

function(kdebase_install_time_path var dir)
  if(IS_ABSOLUTE "${dir}")
    set(${var} "${dir}" PARENT_SCOPE)
  else()
    set(${var} "\${CMAKE_INSTALL_PREFIX}/${dir}" PARENT_SCOPE)
  endif()
endfunction()

function(kdebase_install_pam_service service)
  if(NOT service)
    return()
  endif()
  install(CODE "
    if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
      execute_process(COMMAND \"${PROJECT_SOURCE_DIR}/mkpamserv\" \"${service}\")
    endif()")
endfunction()

function(kdebase_install_setgid name group)
  # ARGN: more lines for the failure message
  set(_more)
  foreach(_l IN LISTS ARGN)
    string(APPEND _more "
      message(STATUS \"${_l}\")")
  endforeach()
  kdebase_install_time_path(_bindir "${KDE3_BIN_INSTALL_DIR}")
  install(CODE "
    set(_f \"\$ENV{DESTDIR}${_bindir}/${name}\")
    execute_process(COMMAND chown \"root:${group}\" \"\${_f}\" RESULT_VARIABLE _r
      OUTPUT_QUIET ERROR_QUIET)
    if(_r EQUAL 0)
      execute_process(COMMAND chmod 2755 \"\${_f}\")
      message(STATUS \"${name} is installed setgid ${group}\")
    else()
      message(STATUS \"Error: Could not install ${name} as setgid ${group}!!\")${_more}
    endif()")
endfunction()

# ---------------------------------------------------------------------
# kdebase_dummy_cpp(<var>)
#
# "dummy.cpp: echo > dummy.cpp": the empty source a module or kdeinit
# program needs whose code is all in convenience libraries.  Sets <var>
# to the generated file in the current binary directory.

function(kdebase_dummy_cpp var)
  set(_f "${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp")
  get_property(_done DIRECTORY PROPERTY _KDEBASE_DUMMY_CPP)
  if(NOT _done)
    set_property(DIRECTORY PROPERTY _KDEBASE_DUMMY_CPP TRUE)
    add_custom_command(OUTPUT "${_f}"
      COMMAND ${CMAKE_COMMAND} -E touch "${_f}"
      VERBATIM)
  endif()
  set(${var} "${_f}" PARENT_SCOPE)
endfunction()
