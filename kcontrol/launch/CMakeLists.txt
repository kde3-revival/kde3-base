kde3_add_module(kcm_launch
  SOURCES
    kcmlaunch.cpp
  MOC
    kcmlaunch.h
  LINK
    KDE3::kdeui
)

install(FILES
  kcmlaunch.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
