kde3_add_module(kickermenu_recentdocs
  SOURCES
    recentdocsmenu.cpp
  MOC
    recentdocsmenu.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  recentdocs.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
