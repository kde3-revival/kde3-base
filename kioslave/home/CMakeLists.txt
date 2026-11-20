kde3_add_library(kiohome OBJECT
  SOURCES
    kio_home.cpp
    homeimpl.cpp
  MOC
    homeimpl.h
  LINK
    KDE3::kio
)

kdebase_dummy_cpp(_dummy)
kde3_add_module(kio_home
  SOURCES
    ${_dummy}
  LINK
    kiohome
    KDE3::kio
)

install(FILES
  home.protocol
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

add_subdirectory(kdedmodule)
