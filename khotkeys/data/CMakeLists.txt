install(FILES
  kde32b1.khotkeys
  konqueror_gestures_kde321.khotkeys
  printscreen.khotkeys
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/khotkeys)

install(FILES
  khotkeys_32b1_update.upd
  konqueror_gestures_kde321_update.upd
  khotkeys_printscreen.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)
