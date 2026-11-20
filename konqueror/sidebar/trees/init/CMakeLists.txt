install(FILES
  home.desktop
  root.desktop
  history.desktop
  bookmarks.desktop
  services.desktop
  remote.desktop
  system.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/entries)

install(FILES
  home.desktop
  root.desktop
  history.desktop
  bookmarks.desktop
  services.desktop
  remote.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/kicker_entries)

install(FILES
  virtualfolderadd.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/add)

install(FILES
  history_module.desktop
  dirtree_module.desktop
  bookmarks_module.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/dirtree)

add_subdirectory(remote)
add_subdirectory(services)
