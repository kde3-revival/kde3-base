kde3_add_module(kcm_display
  SOURCES
    display.cpp
  MOC
    display.h
  LINK
    KDE3::kutils
)

install(FILES
  display.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
