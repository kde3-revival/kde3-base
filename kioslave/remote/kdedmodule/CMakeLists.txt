kde3_add_module(kded_remotedirnotify
  SOURCES
    remotedirnotify.cpp
    remotedirnotify.skel
    remotedirnotifymodule.cpp
    remotedirnotifymodule.skel
  LINK
    KDE3::kio
)

install(FILES
  remotedirnotify.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)
