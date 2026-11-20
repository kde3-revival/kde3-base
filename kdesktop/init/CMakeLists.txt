install(FILES
  directory.trash
  directory.autostart
  directory.templates
  directory.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kdesktop)

install(FILES
  Home.desktop
  System.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kdesktop/DesktopLinks)

add_subdirectory(Templates)
