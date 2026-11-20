kde3_add_module(kio_tar
  SOURCES
    tar.cc
  LINK
    KDE3::kio
)

install(FILES
  tar.protocol
  ar.protocol
  zip.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
