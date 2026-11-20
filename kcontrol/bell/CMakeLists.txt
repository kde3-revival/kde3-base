kde3_add_module(kcm_bell
  SOURCES
    bell.cpp
  MOC
    bell.h
  LINK
    KDE3::kdeui
)

install(FILES
  bell.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
