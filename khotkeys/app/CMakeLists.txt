include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/../shared"
)

kde3_add_module(kded_khotkeys
  SOURCES
    kded.cpp
    kded.skel
  MOC
    kded.h
  LINK
    khotkeys_shared
    KDE3::kdecore
)

kde3_add_kdeinit_executable(khotkeys
  SOURCES
    app.cpp
    app.skel
  MOC
    app.h
  LINK
    khotkeys_shared
    KDE3::kdecore
)

install(FILES
  khotkeys.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)
