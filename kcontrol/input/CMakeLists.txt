# have_xcursor: THEMEPAGE_SUBDIR is xcursor, core otherwise
if(HAVE_XCURSOR)
  set(_themepage xcursor)
else()
  set(_themepage core)
endif()
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/${_themepage}"
)

kde3_add_module(kcm_input
  SOURCES
    mouse.cpp
    kmousedlg.ui
    main.cpp
    logitechmouse.cpp
    logitechmouse_base.ui
  MOC
    logitechmouse.h
    mouse.h
  LINK
    themepage
    ${LIB_XTEST}
    KDE3::kio
    ${LIB_XCURSOR}
    ${USB_LIBS}
)

kde3_add_executable(kapplymousetheme
  SOURCES
    kapplymousetheme.cpp
  LINK
    ${LIB_XCURSOR}
    X11::X11
)

install(FILES
  mouse.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  cursor_large_black.pcf.gz
  cursor_large_white.pcf.gz
  cursor_small_white.pcf.gz
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kcminput)

install(FILES
  mouse_cursor_theme.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

# install-data-hook: the directory for overriding fonts of the cursor
# themes, with a fonts.dir ("-mkfontdir": failures are ignored)
find_program(MKFONTDIR_EXECUTABLE mkfontdir)
mark_as_advanced(MKFONTDIR_EXECUTABLE)
kdebase_install_time_path(_overridedir "${KDE3_DATA_ROOT_INSTALL_DIR}/fonts/override")
install(DIRECTORY DESTINATION ${KDE3_DATA_ROOT_INSTALL_DIR}/fonts/override)
if(MKFONTDIR_EXECUTABLE)
  install(CODE "execute_process(COMMAND \"${MKFONTDIR_EXECUTABLE}\"
    \"\$ENV{DESTDIR}${_overridedir}\" RESULT_VARIABLE _r)")
else()
  install(CODE "message(STATUS \"mkfontdir not found: no fonts.dir in ${_overridedir}\")")
endif()

add_subdirectory(${_themepage})
add_subdirectory(pics)
