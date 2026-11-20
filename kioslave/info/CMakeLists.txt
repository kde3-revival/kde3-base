kde3_add_module(kio_info
  SOURCES
    info.cc
  LINK
    KDE3::kio
)

install(FILES
  info.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  kde-info2html.conf
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kio_info)

install(PROGRAMS
  kde-info2html
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kio_info)
