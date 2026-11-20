kde3_add_module(kded_favicons
  SOURCES
    favicons.cpp
    favicons.skel
  MOC
    favicons.h
  LINK
    KDE3::kio
)

install(FILES
  favicons.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)

install(FILES
  favicons.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  move_favicons.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)
