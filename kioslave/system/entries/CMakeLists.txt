install(FILES
  home.desktop
  documents.desktop
  users.desktop
  media.desktop
  remote.desktop
  trash.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/systemview)

install(PROGRAMS
  kio_system_documenthelper
  DESTINATION ${KDE3_BIN_INSTALL_DIR})
