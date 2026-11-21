kde3_add_executable(kwin_update_window_settings
  DESTINATION ${KDE3_LIB_INSTALL_DIR}/kconf_update_bin
  SOURCES
    update_window_settings.cpp
  LINK
    KDE3::kdecore
)

kde3_add_executable(kwin_update_default_rules
  DESTINATION ${KDE3_LIB_INSTALL_DIR}/kconf_update_bin
  SOURCES
    update_default_rules.cpp
  LINK
    KDE3::kdecore
)

install(FILES
  pop.wav
  DESTINATION ${KDE3_SOUND_INSTALL_DIR})

install(FILES
  kwin.upd
  kwinsticky.upd
  kwiniconify.upd
  kwin3_plugin.upd
  kwin_focus1.upd
  kwinupdatewindowsettings.upd
  kwin_focus2.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  pluginlibFix.pl
  kwin3_plugin.pl
  kwin_focus1.sh
  kwin_focus2.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)
