include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../libkonq"
)

kde3_add_module(konqsidebar_web
  SOURCES
    web_module.cpp
  MOC
    web_module.h
  LINK
    KDE3::kparts
    KDE3::khtml
    konqsidebarplugin
    konq
)

install(FILES
  webmodule_add.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/add)

install(FILES
  websidebar.html
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqsidebartng/websidebar)
