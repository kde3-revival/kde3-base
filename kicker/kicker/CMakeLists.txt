add_custom_command(OUTPUT dummy.cpp
  COMMAND ${CMAKE_COMMAND} -E touch dummy.cpp
  VERBATIM)
kde3_add_kdeinit_executable(kicker
  SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp"
  LINK
    kicker_core
    kicker_buttons
    kicker_ui
    kickermain
    KDE3::kio
    KDE3::kutils
)

kde3_add_executable(kicker-3.4-reverseLayout
  DESTINATION ${KDE3_LIB_INSTALL_DIR}/kconf_update_bin
  SOURCES
    kicker-3.4-reverseLayout.cpp
  LINK
    KDE3Qt::Qt
    KDE3::kdecore
)

install(FILES
  panel.desktop
  DESTINATION ${KDE3_AUTOSTART_INSTALL_DIR})

install(FILES
  kcmkicker.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kickerrc.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  kicker-3.1-properSizeSetting.pl
  kicker-3.5-taskbarEnums.pl
  kicker-3.5-kconfigXTize.pl
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

add_subdirectory(core)
add_subdirectory(ui)
add_subdirectory(buttons)
