kde3_add_module(kded_homedirnotify
  SOURCES
    homedirnotify.cpp
    homedirnotify.skel
    homedirnotifymodule.cpp
    homedirnotifymodule.skel
  LINK
    KDE3::kio
)

install(FILES
  homedirnotify.desktop
  DESTINATION ${KDE3_SERVICES_INSTALL_DIR}/kded)
