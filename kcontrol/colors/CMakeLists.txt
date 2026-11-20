kde3_add_module(kcm_colors
  SOURCES
    colorscm.cpp
    widgetcanvas.cpp
  MOC
    colorscm.h
    widgetcanvas.h
  LINK
    KDE3::kdeui
    KDE3::kio
    krdb
)

install(FILES
  colors.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
