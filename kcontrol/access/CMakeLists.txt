kde3_add_module(kcm_access
  SOURCES
    kcmaccess.cpp
  MOC
    kcmaccess.h
  LINK
    KDE3::kio
)

kde3_add_kdeinit_executable(kaccess
  SOURCES
    kaccess.cpp
    main.cpp
  MOC
    kaccess.h
  LINK
    KDE3::kdeui
)

install(FILES
  kcmaccess.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kaccess.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  eventsrc
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kaccess/)
