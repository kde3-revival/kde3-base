kde3_add_library(kioremote OBJECT
  SOURCES
    kio_remote.cpp
    remoteimpl.cpp
  LINK
    KDE3::kio
)

kdebase_dummy_cpp(_dummy)
kde3_add_module(kio_remote
  SOURCES
    ${_dummy}
  LINK
    kioremote
    KDE3::kio
)

install(FILES
  remote.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(kdedmodule)
