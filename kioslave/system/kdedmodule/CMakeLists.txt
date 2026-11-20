kde3_add_module(kded_systemdirnotify
  SOURCES
    systemdirnotify.cpp
    systemdirnotify.skel
    systemdirnotifymodule.cpp
    systemdirnotifymodule.skel
  LINK
    KDE3::kio
)

install(FILES
  systemdirnotify.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)
