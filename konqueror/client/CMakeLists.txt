include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/.."
)

add_compile_definitions(QT_NO_CAST_ASCII)

kde3_add_kdeinit_executable(kfmclient
  SOURCES
    kfmclient.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/../KonquerorIface.stub
    ${PROJECT_SOURCE_DIR}/kdesktop/KDesktopIface.stub
  MOC
    kfmclient.h
  LINK
    KDE3::kio
)

install(FILES
  kfmclient_3_2.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  kfmclient_3_2_update.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)
