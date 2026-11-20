# config.ci: the configuration tables of config.def for backend and frontend
find_package(Perl REQUIRED)
add_custom_command(OUTPUT config.ci
  COMMAND ${PERL_EXECUTABLE} -w "${CMAKE_CURRENT_SOURCE_DIR}/confproc.pl"
          "${CMAKE_CURRENT_SOURCE_DIR}/config.def" config.ci
  DEPENDS config.def confproc.pl
  COMMENT "Generating config.ci"
  VERBATIM)
add_custom_target(kdm_config_ci DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/config.ci")

install(FILES
  README
  DESTINATION ${KDE3_DATA_ROOT_INSTALL_DIR}/doc/kdm)

# install-data-local: the PAM services (only without DESTDIR)
if(KDM_PAM_SERVICE)
  install(CODE "
    if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
      execute_process(COMMAND \"${PROJECT_SOURCE_DIR}/mkpamserv\" \"${KDM_PAM_SERVICE}\"
        RESULT_VARIABLE _r)
      if(_r EQUAL 0)
        execute_process(COMMAND \"${PROJECT_SOURCE_DIR}/mkpamserv\" -P \"${KDM_PAM_SERVICE}-np\")
      endif()
    endif()")
endif()

add_subdirectory(backend)
add_subdirectory(kfrontend)
