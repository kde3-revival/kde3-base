kde3_add_module(kcm_nic
  SOURCES
    nic.cpp
  MOC
    nic.h
  LINK
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  nic.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
