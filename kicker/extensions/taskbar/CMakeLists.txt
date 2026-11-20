include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../../taskmanager"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../taskbar"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../libkicker"
  "${PROJECT_BINARY_DIR}/kicker/libkicker"
)

kde3_add_module(taskbar_panelextension
  SOURCES
    taskbarextension.cpp
    taskbarextension.skel
  MOC
    taskbarextension.h
  LINK
    KDE3::kdeui
    KDE3::kparts
    kickermain
    taskmanager
    taskbar
)

install(FILES
  taskbarextension.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/extensions)
