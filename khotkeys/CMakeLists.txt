install(FILES
  khotkeys.desktop
  DESTINATION ${KDE3_AUTOSTART_INSTALL_DIR})

add_subdirectory(shared)
add_subdirectory(kcontrol)
add_subdirectory(app)
add_subdirectory(data)
add_subdirectory(update)
