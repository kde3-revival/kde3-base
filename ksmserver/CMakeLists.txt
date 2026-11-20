include_directories(
  "${PROJECT_SOURCE_DIR}/kdmlib"
)

kde3_add_kdeinit_executable(ksmserver
  SOURCES
    main.cpp
    server.cpp
    shutdowndlg.cpp
    legacy.cpp
    startup.cpp
    shutdown.cpp
    client.cpp
    KSMServerInterface.skel
    server.skel
  MOC
    server.h
    shutdowndlg.h
  LINK
    dmctl
    KDE3::kdeui
)

install(FILES
  shutdownkonq.png
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/ksmserver/pics)

install(FILES
  ksmserver.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  move_session_config.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)
