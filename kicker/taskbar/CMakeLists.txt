include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../taskmanager"
  "${CMAKE_CURRENT_SOURCE_DIR}/../libkicker"
  "${PROJECT_BINARY_DIR}/kicker/libkicker"
)

kde3_add_library(taskbar SHARED VERSION_INFO 3:0:2
  SOURCES
    taskbarsettings.kcfgc
    taskcontainer.cpp
    taskbar.cpp
    taskbarcontainer.cpp
    taskbarcontainer.skel
  MOC
    taskbar.h
    taskbarcontainer.h
    taskcontainer.h
  LINK
    KDE3::kdeui
    KDE3::kutils
    kickermain
    taskmanager
)

install(FILES
  taskbar.kcfg
  DESTINATION ${KDE3_KCFG_INSTALL_DIR})
