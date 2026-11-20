kde3_add_module(run_panelapplet
  SOURCES
    runapplet.cpp
  MOC
    runapplet.h
  LINK
    KDE3::kio
    KDE3::kdeui
)

install(FILES
  runapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
