kde3_add_module(kickermenu_kdeprint
  SOURCES
    print_mnu.cpp
  MOC
    print_mnu.h
  LINK
    KDE3::kdeui
    KDE3::kio
    KDE3::kdeprint
)

install(FILES
  printmenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
