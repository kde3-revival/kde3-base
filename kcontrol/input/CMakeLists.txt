# have_xcursor: THEMEPAGE_SUBDIR is xcursor, core otherwise
if(HAVE_XCURSOR)
  set(_themepage xcursor)
else()
  set(_themepage core)
endif()
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/${_themepage}"
)

# the Touchpad tab (libinput X driver's touchpads), with XInput 2
set(_touchpad_sources)
set(_touchpad_moc)
set(_touchpad_link)
if(X11_Xi_FOUND)
  add_subdirectory(touchpad)
  set(_touchpad_sources touchpadpage.cpp)
  set(_touchpad_moc touchpadpage.h)
  set(_touchpad_link touchpadsettings)
endif()

kde3_add_module(kcm_input
  SOURCES
    mouse.cpp
    kmousedlg.ui
    main.cpp
    logitechmouse.cpp
    logitechmouse_base.ui
    ${_touchpad_sources}
  MOC
    logitechmouse.h
    mouse.h
    ${_touchpad_moc}
  LINK
    themepage
    ${_touchpad_link}
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
kde3_install_time_path(_overridedir "${KDE3_DATA_ROOT_INSTALL_DIR}/fonts/override")
install(DIRECTORY DESTINATION ${KDE3_DATA_ROOT_INSTALL_DIR}/fonts/override)
if(MKFONTDIR_EXECUTABLE)
  install(CODE "execute_process(COMMAND \"${MKFONTDIR_EXECUTABLE}\"
    \"\$ENV{DESTDIR}${_overridedir}\" RESULT_VARIABLE _r)")
else()
  install(CODE "message(STATUS \"mkfontdir not found: no fonts.dir in ${_overridedir}\")")
endif()

if(X11_Xi_FOUND)
  target_compile_definitions(kcm_input PRIVATE KCM_INPUT_TOUCHPAD)
  target_include_directories(kcm_input PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/touchpad")
endif()

add_subdirectory(${_themepage})
add_subdirectory(pics)
