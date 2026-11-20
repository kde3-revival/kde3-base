kde3_add_module(kickermenu_konsole
  SOURCES
    konsole_mnu.cpp
    konsolebookmarkhandler.cpp
    konsolebookmarkmenu.cpp
  MOC
    konsole_mnu.h
    konsolebookmarkhandler.h
    konsolebookmarkmenu.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  konsolemenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
