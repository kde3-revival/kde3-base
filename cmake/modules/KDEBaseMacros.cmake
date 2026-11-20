#[=======================================================================[.rst:
KDEBaseMacros
-------------

Install hooks of the kdebase Makefile.am files that the KDE3 package of
kdelibs has no function for.

``kdebase_install_pam_service(<service>)``
  ``install-data-local: -@test -n "$(DESTDIR)" || test -z "$(PAM)" ||
  $(top_srcdir)/mkpamserv $(PAM)``: create the PAM service definition
  with mkpamserv when installing without DESTDIR and PAM is used.
``kdebase_install_setgid(<program> <group>)``
  ``install-exec-hook: (chown root:<group> $(bindir)/<program> && chmod
  2755 ...) || echo "Error: ..."``.
#]=======================================================================]

include_guard(GLOBAL)

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
  install(CODE "
    set(_f \"\$ENV{DESTDIR}${KDE3_FULL_BIN_INSTALL_DIR}/${name}\")
    execute_process(COMMAND chown \"root:${group}\" \"\${_f}\" RESULT_VARIABLE _r
      OUTPUT_QUIET ERROR_QUIET)
    if(_r EQUAL 0)
      execute_process(COMMAND chmod 2755 \"\${_f}\")
      message(STATUS \"${name} is installed setgid ${group}\")
    else()
      message(STATUS \"Error: Could not install ${name} as setgid ${group}!!\")
    endif()")
endfunction()
