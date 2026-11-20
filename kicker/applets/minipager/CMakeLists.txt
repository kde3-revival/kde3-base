include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../taskmanager"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
  "${CMAKE_CURRENT_BINARY_DIR}/../../libkicker"
)

kde3_add_module(minipager_panelapplet
  SOURCES
    pagerapplet.cpp
    pagerbutton.cpp
    pagersettings.kcfgc
  MOC
    pagerapplet.h
    pagerbutton.h
  LINK
    KDE3::kdeui
    taskmanager
    kickermain
)

install(FILES
  minipagerapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)

kde3_install_icons(kpager)
