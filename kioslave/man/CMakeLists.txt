# man2html (EXTRA_PROGRAMS, with man2html_simple.o) is not built by default
kde3_add_module(kio_man
  SOURCES
    man2html.cpp
    kio_man.cpp
  MOC
    kio_man.h
  LINK
    KDE3::kio
)

kde3_add_module(libkmanpart
  SOURCES
    kmanpart.cpp
  MOC
    kmanpart.h
  LINK
    KDE3::khtml
    KDE3::kparts
)

install(FILES
  man.protocol
  kmanpart.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  kio_man.css
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kio_man)
