include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
  "${CMAKE_CURRENT_BINARY_DIR}/../../libkicker"
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(kickermenu_prefmenu
  SOURCES
    prefmenu.cpp
  MOC
    prefmenu.h
  LINK
    KDE3::kdeui
    kickermain
)

install(FILES
  prefmenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
