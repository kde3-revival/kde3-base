kde3_add_module(kcm_smserver
  SOURCES
    kcmsmserver.cpp
    smserverconfigdlg.ui
    smserverconfigimpl.cpp
  MOC
    kcmsmserver.h
    smserverconfigimpl.h
  LINK
    KDE3::kdeui
)

install(FILES
  kcmsmserver.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
