include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(media_panelapplet
  SOURCES
    preferencesdialog.cpp
    mediumbutton.cpp
    mediaapplet.cpp
  MOC
    mediaapplet.h
    mediumbutton.h
    preferencesdialog.h
  LINK
    konq
    kickermain
    KDE3::kdeui
    KDE3::kio
    KDE3::kutils
)

install(FILES
  mediaapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
