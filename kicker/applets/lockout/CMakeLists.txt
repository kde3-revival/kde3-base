include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(lockout_panelapplet
  SOURCES
    lockout.cpp
  MOC
    lockout.h
  LINK
    kickermain
    KDE3::kio
)

install(FILES
  lockout.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
