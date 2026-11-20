kde3_add_module(kcm_kdnssd
  SOURCES
    configdialog.ui
    kcmdnssd.cpp
  MOC
    kcmdnssd.h
  LINK
    KDE3::kdnssd
    KDE3::kdeui
)

install(FILES
  kcm_kdnssd.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
