include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../taskbar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../taskmanager"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
)

kde3_add_module(taskbar_panelapplet
  SOURCES
    taskbarapplet.cpp
  MOC
    taskbarapplet.h
  LINK
    KDE3::kdeui
    taskbar
    kickermain
)

install(FILES
  taskbarapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
