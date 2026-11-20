kde3_add_module(kio_about
  SOURCES
    kio_about.cpp
  LINK
    KDE3::kio
)

install(FILES
  about.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
