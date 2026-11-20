kde3_add_module(menu_panelapplet
  SOURCES
    menuapplet.cpp
    menuapplet.skel
  MOC
    menuapplet.h
  LINK
    KDE3::kdeui
)

install(FILES
  menuapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
