kde3_add_module(kcm_performance
  SOURCES
    kcmperformance.cpp
    konqueror.cpp
    konqueror_ui.ui
    system.cpp
    system_ui.ui
  MOC
    kcmperformance.h
    konqueror.h
    system.h
  LINK
    KDE3::kdeui
)

install(FILES
  kcmperformance.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kcmkonqyperformance.desktop
  DESTINATION ${KDE3_APPS_INSTALL_DIR}/.hidden)
