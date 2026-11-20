include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(naughty_panelapplet
  SOURCES
    NaughtyProcessMonitor.cpp
    NaughtyConfigDialog.cpp
    NaughtyApplet.cpp
  MOC
    NaughtyApplet.h
    NaughtyConfigDialog.h
    NaughtyProcessMonitor.h
  LINK
    kickermain
    KDE3::kdeui
    ${LIB_KVM}
)

install(FILES
  naughty-happy.png
  naughty-sad.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/naughtyapplet/pics)

install(FILES
  naughtyapplet.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)
