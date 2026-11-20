include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(trash_panelapplet
  SOURCES
    trashbutton.cpp
    trashapplet.cpp
  MOC
    trashapplet.h
    trashbutton.h
  LINK
    kickermain
    konq
    KDE3::kdeui
    KDE3::kio
    KDE3::kutils
)

install(FILES
  trashapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
