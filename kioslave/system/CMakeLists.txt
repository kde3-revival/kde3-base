kde3_add_library(kiosystem OBJECT
  SOURCES
    kio_system.cpp
    systemimpl.cpp
  MOC
    systemimpl.h
  LINK
    KDE3::kio
)

kdebase_dummy_cpp(_dummy)
kde3_add_module(kio_system
  SOURCES
    ${_dummy}
  LINK
    kiosystem
    KDE3::kio
)

install(FILES
  system.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(kdedmodule)
add_subdirectory(entries)
add_subdirectory(mimetypes)
