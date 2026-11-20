include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(systemtray_panelapplet
  SOURCES
    systemtrayapplet.cpp
    systemtrayapplet.skel
  MOC
    systemtrayapplet.h
  LINK
    kickermain
    KDE3::kdeui
    KDE3::kio
)

install(FILES
  systemtrayapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
