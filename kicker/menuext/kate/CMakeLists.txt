kde3_add_module(kickermenu_kate
  SOURCES
    katesessionmenu.cpp
  MOC
    katesessionmenu.h
  LINK
    KDE3::kdeui
)

install(FILES
  katesessionmenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
