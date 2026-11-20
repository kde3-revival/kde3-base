include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_module(konq_remoteencoding
  SOURCES
    kremoteencodingplugin.cpp
  MOC
    kremoteencodingplugin.h
  LINK
    konq
)

install(FILES
  kremoteencodingplugin.rc
  kremoteencodingplugin.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqiconview/kpartplugins)

install(FILES
  kremoteencodingplugin.rc
  kremoteencodingplugin.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/konqlistview/kpartplugins)
