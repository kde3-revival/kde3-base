kde3_add_module(kickermenu_find
  SOURCES
    findmenu.cpp
  MOC
    findmenu.h
  LINK
    KDE3::kdeui
)

install(FILES
  find.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)

install(FILES
  kfind.desktop
  websearch.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext/find)
