kde3_add_library(klipper_common OBJECT
  SOURCES
    main.cpp
    toplevel.cpp
    urlgrabber.cpp
    configdialog.cpp
    toplevel.skel
    clipboardpoll.cpp
    history.cpp
    historyitem.cpp
    historystringitem.cpp
    klipperpopup.cpp
    popupproxy.cpp
    historyimageitem.cpp
    historyurlitem.cpp
  MOC
    clipboardpoll.h
    configdialog.h
    history.h
    klipperpopup.h
    popupproxy.h
    toplevel.h
    urlgrabber.h
  LINK
    KDE3::kdeui
    KDE3::kio
    ${LIB_XFIXES}
)

kde3_add_module(klipper_panelapplet
  SOURCES
    applet.cpp
    applet.skel
  MOC
    applet.h
  LINK
    klipper_common
)

add_custom_command(OUTPUT dummy.cpp
  COMMAND ${CMAKE_COMMAND} -E touch dummy.cpp
  VERBATIM)
kde3_add_kdeinit_executable(klipper
  SOURCES
    "${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp"
  LINK
    klipper_common
)

install(FILES
  klipper.desktop
  DESTINATION ${KDE3_XDG_APPS_INSTALL_DIR})

install(FILES
  klipper.desktop
  DESTINATION ${KDE3_AUTOSTART_INSTALL_DIR})

install(FILES
  klipper.desktop
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kicker/applets)

install(FILES
  klipperrc.upd
  klippershortcuts.upd
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

install(PROGRAMS
  klipper-1-2.pl
  klipper-kde31.sh
  DESTINATION ${KDE3_DATA_INSTALL_DIR}/kconf_update)

# install-data-local
install(FILES
  klipperrc.desktop
  DESTINATION ${KDE3_CONFIG_INSTALL_DIR} RENAME klipperrc)

kde3_install_icons(klipper)
