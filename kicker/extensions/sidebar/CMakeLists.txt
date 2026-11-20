include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_module(sidebar_panelextension
  SOURCES
    sidebarextension.cpp
  MOC
    sidebarextension.h
  LINK
    KDE3::kdeui
    KDE3::kparts
    konq
)

install(FILES
  sidebarextension.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/extensions)
