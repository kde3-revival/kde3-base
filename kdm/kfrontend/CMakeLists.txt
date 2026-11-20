include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../backend"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/.."
  "${PROJECT_SOURCE_DIR}/kcontrol/background"
  "${PROJECT_SOURCE_DIR}/kdmlib"
)

kde3_add_executable(kdm_config
  SOURCES
    kdm_config.c
  LINK
    ${LIBRESOLV}
)

kde3_add_executable(kdm_greet
  SOURCES
    kdm_greet.c
    kdmconfig.cpp
    kdmclock.cpp
    kconsole.cpp
    kfdialog.cpp
    kgdialog.cpp
    kchooser.cpp
    kgverify.cpp
    kdmshutdown.cpp
    kgreeter.cpp
    kgapp.cpp
  MOC
    kchooser.h
    kconsole.h
    kdmclock.h
    kdmshutdown.h
    kgapp.h
    kgdialog.h
    kgreeter.h
    kgverify.h
  LINK
    kdmthemer
    KDE3::kdeui
    ${LIB_XTEST}
)

kde3_add_executable(krootimage
  SOURCES
    krootimage.cpp
  MOC
    krootimage.h
  LINK
    bgnd
    KDE3::kio
)

kde3_add_executable(genkdmconf
  SOURCES
    genkdmconf.c
  LINK
    X11::X11
)

kde3_add_executable(kdmctl
  SOURCES
    kdmctl.c
)

# *_COMPILE_FIRST = ../config.ci
foreach(_t kdm_config kdm_greet genkdmconf)
  add_dependencies(${_t} kdm_config_ci)
endforeach()

# install-data-local: genkdmconf writes the configuration (kdmrc and the
# scripts) into $(DESTDIR)$(kde_confdir)/kdm
set(GENKDMCONF_FLAGS "" CACHE STRING "Extra arguments for genkdmconf at installation")
separate_arguments(_genkdmconf_flags UNIX_COMMAND "${GENKDMCONF_FLAGS}")
kdebase_install_time_path(_kdmconfdir "${KDE3_CONFIG_INSTALL_DIR}/kdm")
install(CODE "
  execute_process(COMMAND \"$<TARGET_FILE:genkdmconf>\"
    --in \"\$ENV{DESTDIR}${_kdmconfdir}\" --no-in-notice
    --face-src \"${CMAKE_CURRENT_SOURCE_DIR}/pics\" ${_genkdmconf_flags}
    RESULT_VARIABLE _r)
  if(NOT _r EQUAL 0)
    message(FATAL_ERROR \"genkdmconf failed\")
  endif()")

add_subdirectory(themer)
add_subdirectory(themes)
add_subdirectory(pics)
add_subdirectory(sessions)
