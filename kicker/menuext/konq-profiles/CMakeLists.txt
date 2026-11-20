kde3_add_module(kickermenu_konqueror
  SOURCES
    konqy_menu.cpp
  MOC
    konqy_menu.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  konquerormenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
