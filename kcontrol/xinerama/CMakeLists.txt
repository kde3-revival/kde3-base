kde3_add_module(kcm_xinerama
  SOURCES
    kcmxinerama.cpp
    xineramawidget.ui
  MOC
    kcmxinerama.h
  LINK
    KDE3::kdeui
)

install(FILES
  xinerama.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)
