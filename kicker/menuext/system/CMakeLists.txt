include_directories(
  "${PROJECT_SOURCE_DIR}/kicker/libkicker"
)

kde3_add_module(kickermenu_systemmenu
  SOURCES
    systemmenu.cpp
  MOC
    systemmenu.h
  LINK
    KDE3::kdeui
    KDE3::kio
    kickermain
)

install(FILES
  systemmenu.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/menuext)
