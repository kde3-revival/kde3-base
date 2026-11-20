include_directories(
  "${PROJECT_SOURCE_DIR}/libkonq"
)

kde3_add_library(kfind_common OBJECT
  SOURCES
    kftabdlg.cpp
    kfind.cpp
    kquery.cpp
    kdatecombo.cpp
  MOC
    kdatecombo.h
    kfind.h
    kftabdlg.h
    kquery.h
  LINK
    KDE3::kparts
)

kde3_add_module(libkfindpart
  SOURCES
    kfindpart.cpp
  MOC
    kfindpart.h
  LINK
    kfind_common
    KDE3::kparts
    konq
)

kde3_add_executable(kfind
  SOURCES
    kfwin.cpp
    kfinddlg.cpp
    main.cpp
  MOC
    kfinddlg.h
    kfwin.h
  LINK
    kfind_common
    KDE3::kparts
)

install(FILES
  Kfind.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  kfindpart.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR})

install(FILES
  findpart.desktop
  DESTINATION ${KDE3_SERVICETYPES_INSTALL_DIR})

kde3_install_icons(kfind)

add_subdirectory(pics)
