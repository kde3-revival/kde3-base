include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../ui"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
  "${CMAKE_CURRENT_BINARY_DIR}/../../libkicker"
)

kde3_add_module(kickermenu_remotemenu
  SOURCES
    remotemenu.cpp
    remotemenu.skel
  MOC
    remotemenu.h
  LINK
    KDE3::kdeui
    kickermain
)

install(FILES
  remotemenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
