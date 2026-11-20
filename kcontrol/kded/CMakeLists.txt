kde3_add_module(kcm_kded
  SOURCES
    kcmkded.cpp
  MOC
    kcmkded.h
  LINK
    KDE3::kio
)

install(FILES
  kcmkded.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
