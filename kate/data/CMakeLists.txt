install(FILES
  kateplugin.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

install(FILES
  kate.desktop
  kwrite.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kateui.rc
  externaltools
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kate)

install(FILES
  kwriteui.rc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kwrite)

install(FILES
  tips
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kate)

install(FILES
  kate-2.4.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(FILES
  katerc
  DESTINATION ${KDE3_CONFIG_INSTALL_DIR})
