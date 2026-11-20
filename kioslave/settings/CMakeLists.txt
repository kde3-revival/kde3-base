kde3_add_module(kio_settings
  SOURCES
    kio_settings.cc
  LINK
    KDE3::kio
)

install(FILES
  settings.protocol
  programs.protocol
  applications.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})
