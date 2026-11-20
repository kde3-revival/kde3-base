install(FILES
  konsole.desktop
  konsolesu.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  konsolepart.desktop
  kwrited.desktop
  konsole-script.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  terminalemulator.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

install(FILES
  konsole.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  schemaStrip.pl
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(FILES
  kwrited.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)

install(FILES
  konsolehere.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqueror/servicemenus)

install(FILES
  eventsrc
  tips
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konsole)

# install-data-local: kwrited is a kded module now
install(CODE "file(REMOVE \"\$ENV{DESTDIR}${KDE3_FULL_AUTOSTART_INSTALL_DIR}/kwrited.desktop\")")

add_subdirectory(konsole)
add_subdirectory(other)
add_subdirectory(doc)
add_subdirectory(fonts)
