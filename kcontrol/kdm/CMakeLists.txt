include_directories(
  "${PROJECT_BINARY_DIR}/kcontrol/background"
  "${PROJECT_SOURCE_DIR}/kcontrol/background"
  "${PROJECT_SOURCE_DIR}/kdesktop"
)

kde3_add_module(kcm_kdm
  SOURCES
    background.cpp
    kdm-appear.cpp
    kdm-font.cpp
    kdm-shut.cpp
    kdm-users.cpp
    kdm-conv.cpp
    main.cpp
    kbackedcombobox.cpp
  MOC
    background.h
    kdm-appear.h
    kdm-conv.h
    kdm-font.h
    kdm-shut.h
    kdm-users.h
    main.h
  LINK
    kcmbgnd
    bgnd
    KDE3::kio
    KDE3::knewstuff
)

install(FILES
  kdm.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})
